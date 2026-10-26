//
// main.cpp
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modifications Copyright (C) 2026 aminch <https://github.com/aminch>
// Modified for BMA800XL from BMC64's src/main.cpp: the kernel is a static
// object, and Run() returns BMA800XL's TShutdownMode.
//
// Modified for BMCoco.
//

#include "kernel.h"
#include <circle/startup.h>

int main(void) {
  // The kernel is large; keep it off the stack.
  static CKernel Kernel;
  if (!Kernel.Initialize()) {
    halt();
    return EXIT_HALT;
  }

  TShutdownMode ShutdownMode = Kernel.Run();

  switch (ShutdownMode) {
  case ShutdownReboot:
    reboot();
    return EXIT_REBOOT;

  case ShutdownHalt:
  default:
    halt();
    return EXIT_HALT;
  }
}
