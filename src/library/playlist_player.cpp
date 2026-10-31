#include "library/playlist_player.h"

#include <algorithm>
#include <numeric>

namespace sp {

namespace {
constexpr size_t kMaxHistory = 200;
}

PlaylistPlayer::PlaylistPlayer(uint64_t seed) : rng_(seed) {}

int PlaylistPlayer::indexOf(const std::string& id) const {
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i] == id) return static_cast<int>(i);
    return -1;
}

bool PlaylistPlayer::contains(const std::string& id) const { return indexOf(id) >= 0; }

size_t PlaylistPlayer::playableCount() const {
    size_t n = 0;
    for (const std::string& id : items_) n += failed_.count(id) ? 0 : 1;
    return n;
}

void PlaylistPlayer::setItems(const std::vector<std::string>& ids) {
    if (ids == items_) return;
    items_ = ids;
    // Keep the history meaningful: drop entries no longer in the list.
    history_.erase(std::remove_if(history_.begin(), history_.end(), [&](const std::string& id) { return !contains(id); }),
                   history_.end());
    if (!contains(current_)) current_.clear();
    if (shuffle_) reshuffle(current_);
}

void PlaylistPlayer::setShuffle(bool on) {
    if (on == shuffle_) return;
    shuffle_ = on;
    if (on) reshuffle(current_);
}

// New random order. The current item (if any) is placed first so that the
// next pick is a different one; otherwise `avoidFirst` doesn't start the order.
void PlaylistPlayer::reshuffle(const std::string& avoidFirst) {
    order_.resize(items_.size());
    std::iota(order_.begin(), order_.end(), 0);
    std::shuffle(order_.begin(), order_.end(), rng_);
    orderPos_ = 0;
    const int cur = indexOf(avoidFirst);
    if (cur >= 0) {
        auto it = std::find(order_.begin(), order_.end(), cur);
        std::iter_swap(order_.begin(), it);
    }
}

void PlaylistPlayer::setCurrent(const std::string& id) {
    if (id == current_) return;
    if (!current_.empty()) pushHistory(current_);
    current_ = contains(id) ? id : "";
    if (shuffle_ && !current_.empty()) {
        // Continue the shuffled order from this item.
        const int idx = indexOf(current_);
        auto it = std::find(order_.begin(), order_.end(), idx);
        if (it != order_.end()) orderPos_ = static_cast<size_t>(it - order_.begin());
    }
}

void PlaylistPlayer::pushHistory(const std::string& id) {
    if (!history_.empty() && history_.back() == id) return;
    history_.push_back(id);
    if (history_.size() > kMaxHistory) history_.erase(history_.begin());
}

std::string PlaylistPlayer::next() {
    if (items_.empty() || playableCount() == 0) return "";
    const size_t n = items_.size();
    if (shuffle_) {
        if (order_.size() != n) reshuffle(current_);
        // Without a current item, start at the beginning of the order.
        long pos = current_.empty() ? -1 : static_cast<long>(orderPos_);
        for (size_t tries = 0; tries < 2 * n; ++tries) {
            if (++pos >= static_cast<long>(n)) {
                const std::string last = items_[static_cast<size_t>(order_[n - 1])];
                reshuffle("");
                // Don't play the same shader twice in a row across reshuffles.
                if (n > 1 && items_[static_cast<size_t>(order_[0])] == last) std::swap(order_[0], order_[1]);
                pos = 0;
            }
            const std::string& id = items_[static_cast<size_t>(order_[static_cast<size_t>(pos)])];
            if (failed_.count(id) || (id == current_ && playableCount() > 1)) continue;
            orderPos_ = static_cast<size_t>(pos);
            if (!current_.empty()) pushHistory(current_);
            current_ = id;
            return id;
        }
        return "";
    }
    const int cur = indexOf(current_);
    for (size_t step = 1; step <= n; ++step) {
        const size_t i = (static_cast<size_t>(cur < 0 ? -1 : cur) + step) % n;
        const std::string& id = items_[i];
        if (failed_.count(id)) continue;
        if (!current_.empty() && id != current_) pushHistory(current_);
        current_ = id;
        return id;
    }
    return "";
}

std::string PlaylistPlayer::previous() {
    if (items_.empty() || playableCount() == 0) return "";
    if (shuffle_) {
        // Walk back through what was actually played.
        while (!history_.empty()) {
            const std::string id = history_.back();
            history_.pop_back();
            if (failed_.count(id) || !contains(id) || id == current_) continue;
            current_ = id;
            const int idx = indexOf(id);
            auto it = std::find(order_.begin(), order_.end(), idx);
            if (it != order_.end()) orderPos_ = static_cast<size_t>(it - order_.begin());
            return id;
        }
        return "";
    }
    const size_t n = items_.size();
    const int cur = indexOf(current_);
    for (size_t step = 1; step <= n; ++step) {
        const size_t start = cur < 0 ? 0 : static_cast<size_t>(cur);
        const size_t i = (start + n * 2 - step) % n;
        const std::string& id = items_[i];
        if (failed_.count(id)) continue;
        current_ = id;
        return id;
    }
    return "";
}

std::string PlaylistPlayer::random() {
    std::vector<size_t> candidates;
    for (size_t i = 0; i < items_.size(); ++i)
        if (!failed_.count(items_[i]) && items_[i] != current_) candidates.push_back(i);
    if (candidates.empty()) {
        // Only the current shader is playable: replay it.
        return contains(current_) && !failed_.count(current_) ? current_ : "";
    }
    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    const std::string id = items_[candidates[pick(rng_)]];
    setCurrent(id);
    return id;
}

}  // namespace sp
