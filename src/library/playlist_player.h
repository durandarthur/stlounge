#pragma once
// Which shader comes next: playlist order, shuffle, history for "previous",
// and skipping shaders that failed to load in this session. Pure logic.

#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace sp {

class PlaylistPlayer {
public:
    explicit PlaylistPlayer(uint64_t seed = std::random_device{}());

    // New contents (library rescan, other playlist...). Keeps the current shader if it's still there.
    void setItems(const std::vector<std::string>& ids);
    const std::vector<std::string>& items() const { return items_; }
    bool contains(const std::string& id) const;

    // Shuffle plays every item once in random order, then reshuffles.
    void setShuffle(bool on);
    bool shuffle() const { return shuffle_; }

    // The shader now playing ("" if none is from this playlist). Call when
    // the user picks one directly, so next/previous continue from there.
    void setCurrent(const std::string& id);
    const std::string& current() const { return current_; }

    // These return the id to play ("" if nothing playable) and make it current.
    std::string next();
    std::string previous();
    std::string random();  // any item but the current one

    // Failed shaders are skipped until clearFailed() (or the app restarts).
    void markFailed(const std::string& id) { failed_.insert(id); }
    bool isFailed(const std::string& id) const { return failed_.count(id) != 0; }
    void clearFailed() { failed_.clear(); }
    size_t playableCount() const;

private:
    void reshuffle(const std::string& avoidFirst);
    int indexOf(const std::string& id) const;
    void pushHistory(const std::string& id);

    std::vector<std::string> items_;
    std::string current_;
    bool shuffle_ = false;
    std::vector<int> order_;  // shuffled indices into items_
    size_t orderPos_ = 0;     // position in order_ of the current item
    std::vector<std::string> history_;  // previously played, most recent last
    std::set<std::string> failed_;
    std::mt19937_64 rng_;
};

}  // namespace sp
