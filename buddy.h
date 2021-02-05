#pragma once

void buddy_init(void* space, int block_num);
int buddy_alloc(void* space, int size);
void buddy_dealloc(void* space, int block_num, int address);
