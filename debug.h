//
// Created by Owner on 2024/02/07.
//

#ifndef NEWDIRECTORY_DEBUG_H
#define NEWDIRECTORY_DEBUG_H

// DEBUGが未定義の場合、デフォルトで無効になるように設定
#define DEBUG 1

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
//#define DEBUG3 1

#ifdef DEBUG3
#define DEBUG_COUT3(x) std::cout << x << std::endl
#else
#define DEBUG_COUT3(x)
#endif


#endif //NEWDIRECTORY_DEBUG_H
