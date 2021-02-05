#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <windows.h>
#include "slab.h"

int n, c;
CRITICAL_SECTION mutex;

struct level {
	int begins;
	int ends;
	struct level* next;
};


void buddy_init(void* space, int block_num) {

	if (space == 0 || block_num <= 0) {
		printf("Initialization not possible - wrong parameters!\n");
		exit(1);
	}

	int availableBuddyMetaData = (block_num / 6) * BLOCK_SIZE;
	if (block_num % 6) availableBuddyMetaData += BLOCK_SIZE;
	int availableBuddy = (block_num / 6) * 5 * BLOCK_SIZE;
	c = availableBuddy / BLOCK_SIZE;
	//printf("c = %d\n", c);

	//printf("buddyMDAvailable je %d\n", availableBuddyMetaData);
	//printf("buddyAvailable je %d\n", availableBuddy);

	if (availableBuddy == 0) {
		printf("Space is too small - can't allocate memory and provide space for buddy metadata!\n");
		exit(1);
	}

	n = ceil(log(availableBuddy) / log(2));
	int x = n;
	if (pow(2, n) > availableBuddy) n -= 1;
	
	//printf("n is %d\n", n);
	for (int i = 0; i < n * c; i += c) {
		struct level temp;
		temp.next = 0;
		memcpy((int)space + i * sizeof(struct level), &temp, sizeof(struct level));
	}

	struct level temp;
	temp.next = (int)space + (c * (n - 1) + 1) * sizeof(struct level);
	memcpy((int)space + (n - 1) * c * sizeof(struct level), &temp, sizeof(struct level));

	temp.next = 0;
	temp.begins = (int)space + availableBuddyMetaData;
	temp.ends = (int)space + availableBuddyMetaData + pow(2,n);
	int endsForNow = temp.ends;
	//printf("biggest block goes from %d to %d\n", temp.begins, temp.ends);
	memcpy((int)space + (c * (n - 1) + 1) * sizeof(struct level), &temp, sizeof(struct level));

	if (x != n) {
		int pom = x;
		int spare = (int)space + availableBuddyMetaData + availableBuddy - temp.ends;
		//printf("spare je %d\n", spare);
		x = ceil(log(spare) / log(2));
		if (pow(2, x) > spare) x -= 1;
		//printf("to je %d stepena\n", n);
		memset((int)space + (c * (x - 1)) * sizeof(struct level) + 8, 1, 4);
		//printf("adresa na kojoj smestamo blok velicine 2 na %d je %d\n", x, (int)space + (n * (x - 1)) * sizeof(struct level));
		temp.next = 0;
		temp.begins = endsForNow;
		temp.ends = endsForNow + pow(2, x);
		//printf("spare block goes from %d to %d\n", temp.begins, temp.ends);
		memcpy((int)space + (c * (x - 1) + 1) * sizeof(struct level), &temp, sizeof(struct level));
	}

	memset((int)space + n * c * sizeof(struct level) + 8, 0, 4);
	//printf("last address of buddy metadata should be %d\n", (int)space + availableBuddyMetaData);
	//printf("last addres that buddy metadata uses is %d\n", (int)space + 2 * n * c * sizeof(struct level));
	InitializeCriticalSection(&mutex);
}

int buddy_alloc(void* space, int size) {
	EnterCriticalSection(&mutex);

	if (space == 0 || size <= 0) {
		printf("Allocation not possible - wrong parameters!\n");
		LeaveCriticalSection(&mutex);
		exit(1);
	}

	int x = ceil(log(size) / log(2));
	//printf("x je %d\n", x);
	if (x > n) {
		printf("Failed to allocate memory - requested block is larger than available memory0\n");
		LeaveCriticalSection(&mutex);
		exit(1);
	}

	char* pointer = (char*)((int)space + (x - 1) * c * sizeof(struct level));

	if (*(pointer + 8) != 0) {

		struct level temp;
		memcpy(&temp, pointer + sizeof(struct level), sizeof(struct level));

		if (*(pointer + sizeof(struct level) + 8) == 0) {
			memset(pointer + 8, 0, 4);
		}
		else {
			memmove(pointer + sizeof(struct level), pointer + 2 * sizeof(struct level), (c - 2) * sizeof(struct level));
			memset(pointer + 8, 1, 4);
		}

		printf("\nAllocated from %d to %d\n", temp.begins, temp.ends);

		char* dealocPointer = (char*)((int)space + n * c * sizeof(struct level));
		if (*(dealocPointer + 8) != 0) {
			while (*(dealocPointer + 8) != 0) dealocPointer += sizeof(struct level);
		}
		temp.ends = temp.ends - temp.begins;
		temp.next = 0;
		memcpy(dealocPointer + sizeof(struct level), &temp, sizeof(struct level));
		memset(dealocPointer + 8, 1, 4);
		memset(dealocPointer + sizeof(struct level) + 8, 0, 4);

		LeaveCriticalSection(&mutex);
		return temp.begins;
	}
	else {
		int i;
		for (i = x - 1; i < n; i++) {
			pointer = (char*)((int)space + i * c * sizeof(struct level));
			if (*(pointer + 8) != 0)
				break;
		}
		if (i == n) {
			printf("Failed to allocate memory - requested block is larger than available memory\n");
			LeaveCriticalSection(&mutex);
			exit(1);
		}
		else {

			struct level temp;
			memcpy(&temp, pointer + sizeof(struct level), sizeof(struct level));

			if (*(pointer + sizeof(struct level) + 8) == 0) {
				memset(pointer + 8, 0, 4);
			}
			else {
				memmove(pointer + sizeof(struct level), pointer + 2 * sizeof(struct level), (c - 2) * sizeof(struct level));
				memset(pointer + 8, 1, 4);
			}

			i--;

			for (; i >= x - 1; i--) {

				struct level temp1, temp2;
				temp1.begins = temp.begins;
				temp1.ends = temp.begins + (temp.ends - temp.begins) / 2;
				temp2.begins = temp.begins + (temp.ends - temp.begins + 1) / 2;
				temp2.ends = temp.ends;

				char* tempPointer = (char*)((int)space + i * c * sizeof(struct level));
				if (*(tempPointer + 8) != 0) {
					while (*(tempPointer + 8) != 0) tempPointer += sizeof(struct level);
				}
				memcpy(tempPointer + sizeof(struct level), &temp1, sizeof(struct level));
				memcpy(tempPointer + 2 * sizeof(struct level), &temp2, sizeof(struct level));
				memset(tempPointer + 8, 1, 4);
				memset(tempPointer + sizeof(struct level) + 8, 1, 4);
				memset(tempPointer + 2 * sizeof(struct level) + 8, 0, 4);

				memcpy(&temp, tempPointer + sizeof(struct level), sizeof(struct level));

				if (*(tempPointer + sizeof(struct level) + 8) == 0) {
					memset(tempPointer + 8, 0, 4);
				}
				else {
					memmove(tempPointer + sizeof(struct level), tempPointer + 2 * sizeof(struct level), (c - 2) * sizeof(struct level));
					memset(tempPointer + 8, 1, 4);
				}

			}

			printf("\nAllocated from %d to %d\n", temp.begins, temp.ends);

			char* dealocPointer = (char*)((int)space + n * c * sizeof(struct level));
			if (*(dealocPointer + 8) != 0) {
				while (*(dealocPointer + 8) != 0) dealocPointer += sizeof(struct level);
			}
			temp.ends = temp.ends - temp.begins;
			temp.next = 0;
			memcpy(dealocPointer + sizeof(struct level), &temp, sizeof(struct level));
			memset(dealocPointer + 8, 1, 4);
			memset(dealocPointer + sizeof(struct level) + 8, 0, 4);

			LeaveCriticalSection(&mutex);
			return temp.begins;
		}
	}
}

void buddy_dealloc(void* space, int block_num, int address) {
	EnterCriticalSection(&mutex);

	if (space == 0 || block_num <= 0 || address <= 0) {
		printf("Deallocation not succesful - wrong parameters!\n");
		LeaveCriticalSection(&mutex);
		exit(1);
	}

	char* dealocPointer = (char*)((int)space + n * c * sizeof(struct level));
	struct level temp;
	temp.begins = 0;
	int dealloc = 0;
	for (int i = 1; i < n * c; i++) {
		dealocPointer += sizeof(struct level);
		dealloc++;
		memcpy(&temp, dealocPointer, sizeof(struct level));
		if (temp.begins == address) break;
		if (temp.next == 0) {
			printf("Deallocation not succesful!\n");
			LeaveCriticalSection(&mutex);
			exit(1);
		}
	}

	int d = ceil(log(temp.ends) / log(2));

	int i, buddyNumber, buddyAddress;

	char* tempPointer = (char*)((int)space + (d - 1) * c * sizeof(struct level));
	if (*(tempPointer + 8) != 0) {
		while (*(tempPointer + 8) != 0) tempPointer += sizeof(struct level);
	}
	temp.ends = temp.begins + temp.ends;
	memset(tempPointer + 8, 1, 4);
	memcpy(tempPointer + sizeof(struct level), &temp, sizeof(struct level));
	memset(tempPointer + sizeof(struct level) + 8, 0, 4);
	printf("\nDeallocated from %d to %d\n", temp.begins, temp.ends);

	buddyNumber = (temp.begins - ((int)space + (BLOCK_SIZE * block_num) / 6)) / (temp.begins - temp.ends);

	if (buddyNumber % 2 != 0)
		buddyAddress = temp.begins - pow(2, d);
	else
		buddyAddress = temp.begins + pow(2, d);

	for (i = 0; i < n; i++) {

		struct level tempBuddy;
		int buddy = 0;
		char* buddyPointer = (char*)((int)space + (d - 1) * c * sizeof(struct level));
		for (int i = 1; i < c; i++) {
			buddyPointer += sizeof(struct level);
			buddy++;
			memcpy(&tempBuddy, buddyPointer, sizeof(struct level));
			if (tempBuddy.begins == buddyAddress) break;
			if (tempBuddy.next == 0) {
				LeaveCriticalSection(&mutex);
				return;
			}
		}

		//now merge the buddies to make them one large memory free block
		if ((buddyNumber % 2) == 0) {

			char* newPointer = (char*)((int)space + d * c * sizeof(struct level));
			if (*(newPointer + 8) != 0) {
				while (*(newPointer + 8) != 0) newPointer += sizeof(struct level);
			}
			struct level tempFinal;
			tempFinal.begins = temp.begins;
			tempFinal.ends = temp.begins + 2 * pow(2, d);
			tempFinal.next = 0;
			memset(newPointer + 8, 1, 4);
			memcpy(newPointer + sizeof(struct level), &tempFinal, sizeof(struct level));
			memset(newPointer + sizeof(struct level) + 8, 0, 4);

			printf("\nMade two buddies into one from %d to %d\n", tempFinal.begins, tempFinal.ends);
		}
		else {

			char* newPointer = (char*)((int)space + d * c * sizeof(struct level));
			if (*(newPointer + 8) != 0) {
				while (*(newPointer + 8) != 0) newPointer += sizeof(struct level);
			}
			struct level tempFinal;
			tempFinal.begins = buddyAddress;
			tempFinal.ends = buddyAddress + 2 * pow(2, d);
			tempFinal.next = 0;
			memset(newPointer + 8, 1, 4);
			memcpy(newPointer + sizeof(struct level), &tempFinal, sizeof(struct level));
			memset(newPointer + sizeof(struct level) + 8, 0, 4);

			printf("\nMade two buddies into one from %d to %d\n", tempFinal.begins, tempFinal.ends);
		}

		memset(tempPointer + 8, 0, 4);
		if (*(buddyPointer + 8) == 0) {
			memset(buddyPointer - 4, 0, 4);
		}
		else {
			memmove(buddyPointer, buddyPointer + sizeof(struct level), (c - buddy) * sizeof(struct level));
		}

		break;
	}

	if (*(dealocPointer + 8) == 0) {
		memset(dealocPointer - 4, 0, 4);
	}
	else {
		memmove(dealocPointer, dealocPointer + sizeof(struct level), (n * c - dealloc) * sizeof(struct level));
	}
	LeaveCriticalSection(&mutex);
}