/* Copyright 2016 Pedro Falcato

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http ://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <stdint.h>
#include <kernel/registers.h>
#include <kernel/panic.h>
void syscall()
{
	uint32_t eax,ebx,ecx,edx,edi;
	asm volatile("mov %%eax,%0":"=a"(eax));
	asm volatile("mov %%ebx,%0":"=a"(ebx));
	asm volatile("mov %%ecx,%0":"=a"(ecx));
	asm volatile("mov %%edx,%0":"=a"(edx));
	asm volatile("mov %%edi,%0":"=a"(edi));
	if(eax == 0)
		terminal_writestring(ebx);
}