/*
 * Copyright (C) 2018 ScyllaDB
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

#include "view_update_generator.hh"

static logging::logger vug_logger("view_update_generator");

namespace db::view {

future<> view_update_generator::start() {
    thread_attributes attr;
    attr.sched_group = _db.get_streaming_scheduling_group();
    _started = seastar::async(std::move(attr), [this]() mutable {
        while (!_as.abort_requested()) {
            if (_sstables_with_tables.empty()) {
                _pending_sstables.wait().get();
            }
            while (!_sstables_with_tables.empty()) {
                auto& [sst, t] = _sstables_with_tables.front();
                try {
                    schema_ptr s = t->schema();
                    flat_mutation_reader staging_sstable_reader = sst->read_rows_flat(s);
                    auto result = staging_sstable_reader.consume_in_thread(view_updating_consumer(s, _proxy, sst, _as), db::no_timeout);
                    if (result == stop_iteration::yes) {
                        break;
                    }
                } catch (...) {
                    vug_logger.warn("Processing {} failed: {}. Will retry...", sst->get_filename(), std::current_exception());
                    break;
                }
                try {
                    // collect all staging sstables to move in a map, grouped by table.
                    _sstables_to_move[t].push_back(sst);
                } catch (...) {
                    // Move from staging will be retried upon restart.
                    vug_logger.warn("Moving {} from staging failed: {}. Ignoring...", sst->get_filename(), std::current_exception());
                }
                _registration_sem.signal();
                _sstables_with_tables.pop_front();
            }
            // For each table, move the processed staging sstables into the table's base dir.
            for (auto it = _sstables_to_move.begin(); it != _sstables_to_move.end(); ) {
                auto& [t, sstables] = *it;
                try {
                    t->move_sstables_from_staging(sstables).get();
                } catch (...) {
                    // Move from staging will be retried upon restart.
                    vug_logger.warn("Moving some sstable from staging failed: {}. Ignoring...", std::current_exception());
                }
                it = _sstables_to_move.erase(it);
            }
        }
    });
    return make_ready_future<>();
}

future<> view_update_generator::stop() {
    _as.request_abort();
    _pending_sstables.signal();
    return std::move(_started).then([this] {
        _registration_sem.broken();
    });
}

bool view_update_generator::should_throttle() const {
    return !_started.available();
}

future<> view_update_generator::register_staging_sstable(sstables::shared_sstable sst, lw_shared_ptr<table> table) {
    if (_as.abort_requested()) {
        return make_ready_future<>();
    }
    _sstables_with_tables.emplace_back(std::move(sst), std::move(table));
    _pending_sstables.signal();
    if (should_throttle()) {
        return _registration_sem.wait(1);
    } else {
        _registration_sem.consume(1);
        return make_ready_future<>();
    }
}

}
