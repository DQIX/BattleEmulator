//
// Created by Owner on 2024/02/07.
//

#ifndef NEWDIRECTORY_DEBUG_H
#define NEWDIRECTORY_DEBUG_H

#include <iostream>

namespace trace {
#ifndef NDEBUG
    void setEnabled(bool value) noexcept;
    bool enabled() noexcept;
#else
    inline void setEnabled(bool) noexcept {}
    inline bool enabled() noexcept { return false; }
#endif
}

#ifndef NDEBUG
#define TRACE(statement) do { if (trace::enabled()) { statement; } } while (false)
#else
#define TRACE(statement) do { } while (false)
#endif

// Release/NDEBUG ではデバッグ経路をコンパイルしない。
#ifndef NDEBUG
#define DEBUG 1
#endif

// DEBUGモードが有効な場合にのみデバッグ出力を有効にする
#ifdef DEBUG
#define DEBUG_COUT(x) std::cout << x << std::endl
#else
#define DEBUG_COUT(x)
#endif

//#define DEBUG1 1

// DEBUGモードが有効な場合にのみデバッグ出力を有効にする
#ifdef DEBUG1
#define DEBUG_COUT1(x) std::cout << x << std::endl
#else
#define DEBUG_COUT1(x)
#endif

//THIS DEBUG CODE!
//#define DEBUG2 1

#ifdef DEBUG2
#define DEBUG_COUT2(x) std::cout << x << std::endl
#else
#define DEBUG_COUT2(x)
#endif

//THIS DEBUG CODE!
#ifndef NDEBUG
//#define DEBUG3 1
#endif

#ifdef DEBUG3
#define DEBUG_COUT3(x) std::cout << x << std::endl
#else
#define DEBUG_COUT3(x)
#endif


#endif //NEWDIRECTORY_DEBUG_H
