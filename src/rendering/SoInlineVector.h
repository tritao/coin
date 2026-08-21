#ifndef COIN_SOINLINEVECTOR_H
#define COIN_SOINLINEVECTOR_H

#include <cstddef>
#include <vector>

// Small contiguous storage for default-constructible internal render records.
// This intentionally implements only the operations used by retained
// construction and is not a general Coin container API.
template <typename T, std::size_t N>
class SoInlineVector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using iterator = value_type *;
  using const_iterator = const value_type *;

  bool empty() const { return this->size() == 0; }
  size_type size() const
  { return this->overflowed ? this->overflow.size() : this->inlineCount; }
  void clear()
  {
    this->inlineCount = 0;
    this->overflow.clear();
    this->overflowed = false;
  }
  void reserve(size_type count)
  { if (count > N) this->overflow.reserve(count); }
  void push_back(const value_type & value)
  {
    if (!this->overflowed && this->inlineCount < N) {
      this->inlineValues[this->inlineCount++] = value;
      return;
    }
    this->moveInlineValuesToOverflow();
    this->overflow.push_back(value);
  }
  void push_back(value_type && value)
  {
    if (!this->overflowed && this->inlineCount < N) {
      this->inlineValues[this->inlineCount++] =
        static_cast<value_type &&>(value);
      return;
    }
    this->moveInlineValuesToOverflow();
    this->overflow.push_back(static_cast<value_type &&>(value));
  }
  value_type & operator[](size_type index)
  { return this->overflowed ? this->overflow[index] : this->inlineValues[index]; }
  const value_type & operator[](size_type index) const
  { return this->overflowed ? this->overflow[index] : this->inlineValues[index]; }
  iterator data()
  { return this->overflowed ? this->overflow.data() : this->inlineValues; }
  const_iterator data() const
  { return this->overflowed ? this->overflow.data() : this->inlineValues; }
  iterator begin() { return this->data(); }
  const_iterator begin() const { return this->data(); }
  iterator end() { return this->data() + this->size(); }
  const_iterator end() const { return this->data() + this->size(); }

private:
  void moveInlineValuesToOverflow()
  {
    if (this->overflowed) return;
    this->overflow.reserve(this->inlineCount + 1);
    for (size_type i = 0; i < this->inlineCount; ++i) {
      this->overflow.push_back(
        static_cast<value_type &&>(this->inlineValues[i]));
    }
    this->overflowed = true;
  }

  value_type inlineValues[N] = {};
  size_type inlineCount = 0;
  std::vector<value_type> overflow;
  bool overflowed = false;
};

#endif // COIN_SOINLINEVECTOR_H
