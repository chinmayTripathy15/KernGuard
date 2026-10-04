#include <iostream>
#include <unistd.h>
int main() {
while(true) {
std::cout << "Heartbeat: I am alive" << std::endl;
sleep(1);
}
return 0;
}
