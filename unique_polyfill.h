#pragma once

// MSVS defines this already.
// http://stackoverflow.com/questions/14131454/visual-studio-2012-cplusplus-and-c-11
#if defined(_MSC_VER) && _MSC_VER < 1800 || !defined(_MSC_VER) && __cplusplus <= 201103L
#include <memory>
#include <utility>
namespace std {
  // C++14 backfill from http://herbsutter.com/gotw/_102/
  template<typename T, typename ...Args>
    inline std::unique_ptr<T> make_unique(Args&& ...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
  }
}
#endif
