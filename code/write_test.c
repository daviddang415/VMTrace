#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
  write(1, "1\n", 2);
  write(1, "2\n", 2);
  write(1, "3\n", 2);
}
