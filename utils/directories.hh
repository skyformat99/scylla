/*
 * Copyright (C) 2019 ScyllaDB
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <unordered_set>
#include <seastar/core/future.hh>
#include <seastar/core/smp.hh>
#include "utils/file_lock.hh"

using namespace seastar;

namespace utils {

class directories {
public:
    future<> init(db::config& cfg, bool hinted_handoff_enabled);
private:
    future<> touch_and_lock(fs::path path);
    std::vector<file_lock> _locks;
};

} // namespace utils
