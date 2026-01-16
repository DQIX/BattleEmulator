#include <cstring>

template<typename T>
class LinearIdPool {
public:
    explicit LinearIdPool(size_t initial = 1024)
        : capacity_(initial), size_(0)
    {
        data_ = static_cast<T*>(std::malloc(sizeof(T) * capacity_));
        if (!data_) std::abort();
        std::memset(data_, 0, sizeof(T) * capacity_);
    }

    ~LinearIdPool() {
        std::free(data_);
    }

    // 値を渡して確保（これが欲しかった形）
    uint32_t alloc(const T& value) {
        uint32_t id = allocRaw();
        data_[id] = value;   // ← ここが正解（コピー代入）
        return id;
    }

    // 空で確保して後から書く場合
    uint32_t allocRaw() {
        if (size_ >= capacity_) {
            grow();
        }
        uint32_t id = size_++;
        std::memset(&data_[id], 0, sizeof(T));
        return id;
    }

    T& get(uint32_t id) {
        return data_[id];
    }

    const T& get(uint32_t id) const {
        return data_[id];
    }

    [[nodiscard]] const int getSize() const { return size_; }

private:
    void grow() {
        size_t newCap = capacity_ * 2;
        T* newData = static_cast<T*>(
            std::realloc(data_, sizeof(T) * newCap)
        );
        if (!newData) std::abort();

        std::memset(
            newData + capacity_,
            0,
            sizeof(T) * (newCap - capacity_)
        );

        data_ = newData;
        capacity_ = newCap;
    }

private:
    T* data_;
    size_t capacity_;
    size_t size_;
};
