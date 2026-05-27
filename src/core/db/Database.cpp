#include "core/db/Database.h"
#include <sqlite3.h>

namespace clickin {

struct Database::Impl {
    sqlite3* db = nullptr;
};

Database::Database(const std::string& path) : path_(path), impl_(new Impl{}) {}

Database::~Database() {
    close();
    delete impl_;
}

bool Database::open() {
    int rc = sqlite3_open(path_.c_str(), &impl_->db);
    return rc == SQLITE_OK;
}

void Database::close() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

bool Database::isOpen() const { return impl_->db != nullptr; }

} // namespace clickin
