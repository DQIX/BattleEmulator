#ifndef LINEAR_ID_POOL_H
#define LINEAR_ID_POOL_H

template<typename T, size_t FAST_CAP>
class LinearIdPool {
public:
    LinearIdPool()
        : capacity_(FAST_CAP), size_(0)
    {
        data_ = static_cast<T*>(std::malloc(sizeof(T) * capacity_));
        if (!data_) std::abort();
    }

    ~LinearIdPool() {
        std::free(data_);
    }

    uint32_t alloc(const T& value) {
        if (size_ >= capacity_) {
            grow();
        }
        uint32_t id = size_++;
        data_[id] = value;
        return id;
    }

    T& get(uint32_t id) {
        return data_[id];
    }

    void reset() {
        size_ = 0;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    int getSize() const { return capacity_; }

private:
    void grow() {
        // 最初の grow だけ遅いが、その後は安定
        size_t newCap = capacity_ + 65550;
        T* newData = static_cast<T*>(
            std::realloc(data_, sizeof(T) * newCap)
        );
        if (!newData) std::abort();
        data_ = newData;
        capacity_ = newCap;
    }

private:
    T* data_;
    size_t capacity_;
    size_t size_;
};

#endif // LINEAR_ID_POOL_H