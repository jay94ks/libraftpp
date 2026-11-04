#include <raftpp/sqlite/sqlite.hpp>
#include "database_contract.hpp"
#include <sqlite3.h>

int main() {
    using namespace database_testing;
    if (run<sqlite_storage>("sqlite"))
        return 1;
    try {
        directory root{"sqlite-native"};
        auto path = root.path / "database";
        {
            sqlite_storage store{path};
            check(!store.init(1, "one") && !store.bootstrap(database_members()), "native setup");
            store.close();
        }
        sqlite3* database = nullptr;
        check(sqlite3_open(path.c_str(), &database) == SQLITE_OK, "native open");
        auto code = sqlite3_exec(
            database,
            "CREATE TRIGGER reject_third BEFORE INSERT ON raftpp_kv WHEN NEW.key=x'650000000000000003' BEGIN SELECT RAISE(ABORT,'injected rollback'); END;",
            nullptr, nullptr, nullptr);
        sqlite3_close(database);
        check(code == SQLITE_OK, "native trigger");
        {
            sqlite_storage store{path};
            check(!store.init(1, "one"), "transaction reopen");
            auto entries = batch(1, "first");
            entries->push_back(
                std::make_shared<entry>(entry{1, entry_type::command, bytes("second")}));
            bool called = false;
            check(static_cast<bool>(store.append(2, entries, [&](error_code) { called = true; })) &&
                      !called,
                  "failed DB transaction is not accepted");
            io_state output;
            check(!store.load(output) && output.entries.size() == 1,
                  "rollback removes the first mutation too");
            store.close();
        }
        check(sqlite3_open(path.c_str(), &database) == SQLITE_OK, "native corrupt open");
        code = sqlite3_exec(database,
                            "UPDATE raftpp_kv SET value=x'00' WHERE key=CAST('meta' AS BLOB)",
                            nullptr, nullptr, nullptr);
        sqlite3_close(database);
        check(code == SQLITE_OK, "native corruption");
        sqlite_storage store{path};
        check(store.init(1, "one") == errc::corrupt, "reject truncated metadata");
        store.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
