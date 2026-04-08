#include <iostream>
#include "buffer.h"

  int main() {
      Buffer buff;

      buff.Append("hello world", 11);
      std::cout << "ReadableBytes: " << buff.ReadableBytes() << std::endl;
      std::cout << "Data: " << buff.Peek() << std::endl;

      buff.Retrieve(6);
      std::cout << "After Retrieve(6), ReadableBytes: " << buff.ReadableBytes() << std::endl;
      std::cout << "Data after Retrieve(6): " << buff.Peek() << std::endl;

      buff.RetrieveAll();
      std::cout << "After RetrieveAll, ReadableBytes: " << buff.ReadableBytes() << std::endl;

      return 0;
  }
