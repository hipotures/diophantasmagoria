#include "search.hpp"
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>

// Isolated subprocess driver: abrupt exit skips all destructors and final commits.
int main(int argc, char **argv) {
    try {
        if (argc != 6)
            throw std::runtime_error("config db output event index required");
        auto db = dio::load_database(argv[2]);
        auto d = dio::domain(dio::parse(dio::read_file(argv[1])), db);
        dio::Options o;
        o.output = argv[3];
        o.threads = 4;
        o.chunk_tiles = 4;
        o.queue_chunks = 4;
        o.checkpoint_tiles = 32;
        o.checkpoint_seconds = 0.01;
        o.trace = true;
        std::string event = argv[4];
        dio::U index = std::stoull(argv[5]);
        std::mutex mutex;
        std::condition_variable ready;
        bool later_finished = false;
        o.observer = [&](std::string_view name, dio::U n) {
            if (event == "reorder") {
                if (name == "chunk_started" && n == 0) {
                    std::unique_lock lock(mutex);
                    if (!ready.wait_for(lock, std::chrono::seconds(10),
                                        [&] { return later_finished; }))
                        throw std::runtime_error("No later chunk finished");
                    // Cross the timed epoch boundary before the first chunk completes.
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                if (name == "chunk_completed" && n > 0) {
                    std::lock_guard lock(mutex);
                    later_finished = true;
                    ready.notify_all();
                }
            } else if (name == event && n >= index) {
                _exit(86);
            }
        };
        return dio::search(d, db, o, 0);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
