#include <raftpp/rocksdb/rocksdb.hpp>
#include "database_contract.hpp"
#include <rocksdb/db.h>

int main() {
    using namespace database_testing;
    if (run<rocksdb_storage>("rocksdb"))
        return 1;
    try {
        directory root{"rocksdb-native"};
        auto path = root.path / "database";
        {
            rocksdb_storage store{path};
            check(!store.init(1, "one") && !store.bootstrap(database_members()), "native setup");
            store.close();
        }
        owned<rocksdb::DB> database;
        check(rocksdb::DB::Open(rocksdb::Options{}, path.string(), &database).ok(), "native open");
        rocksdb::WriteOptions options;
        options.sync = true;
        check(database->Put(options, "meta", "bad").ok(), "native corruption");
        database.reset();
        rocksdb_storage store{path};
        check(store.init(1, "one") == errc::corrupt, "reject truncated metadata");
        store.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
