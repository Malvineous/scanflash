/**
 * @file  device.cpp
 * @brief Platform independent interface to a storage device.
 *
 * Copyright (C) 2012 Adam Nielsen <malvineous@shikadi.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include "device.hpp"

/// Length of the master boot record (in bytes)
#define MBR_LEN 512

/// Size of each block/sector referred to in the MBR
#define MBR_SECTOR_SIZE 512

/// Number of sectors per track for LBA conversion
#define CHS_NUMSECTORS 63

/// Number of heads for LBA conversion
#define CHS_NUMHEADS 16

/// Minimum size of a partition (smaller space is ignored)
#define MBR_MIN_PART_SIZE (16/*megabytes*/ * 1048576 / MBR_SECTOR_SIZE)

/// Partition type for usable space
#define MBR_PTYPE_GOOD 0x0C // FAT32 LBA

/// Partition type for unusable space
#define MBR_PTYPE_BAD 0xFF // Xenix bad block table

/// Write a 32-bit little-endian value to a buffer, regardless of host endianness
void store32le(uint8_t *dest, uint32_t val)
{
	dest[0] = val & 0xFF;
	dest[1] = (val >> 8) & 0xFF;
	dest[2] = (val >> 16) & 0xFF;
	dest[3] = (val >> 24) & 0xFF;
	return;
}

/// Convert an LBA value into CHS
void lba2chs(block_t lba, uint8_t *chs)
{
	unsigned int cyls = lba / (CHS_NUMSECTORS * CHS_NUMHEADS);
	unsigned int heads = (lba / CHS_NUMSECTORS) % CHS_NUMHEADS;
	unsigned int sectors = (lba % CHS_NUMSECTORS) + 1;
	chs[0] = (uint8_t)heads;
	chs[1] = (uint8_t)(((cyls & 0x300) >> 2) | sectors);
	chs[2] = (uint8_t)(cyls & 0xFF);
	return;
}

/// Write an entry in the partition table
void writeEntry(uint8_t *mbr, unsigned int index, block_t start, block_t end,
	uint8_t type)
{
	uint8_t *part = &mbr[0x1BE + index * 16];
	// CHS offset of first sector
	lba2chs(start, &part[0x1]);
	lba2chs(end, &part[0x5]);
	// Partition type
	part[0x4] = type;
	// LBA size
	store32le(&part[0x8], start);
	store32le(&part[0xc], end - start + 1); // +1 to include the end sector number

	return;
}

Device::~Device()
	throw ()
{
}

void Device::writePartitionTable(block_t firstBad, block_t lastBad, block_t size)
	throw (error)
{
	uint8_t mbr[MBR_LEN];
	memset(mbr, 0, MBR_LEN);

	// Generate a random serial number
	*((uint32_t *)&mbr[0x1B8]) = (uint32_t)random();

	block_t start = firstBad / MBR_SECTOR_SIZE;
	// +1 to include the byte in the 512b sector count
	block_t end = (lastBad + 1) / MBR_SECTOR_SIZE;
	block_t num = size / MBR_SECTOR_SIZE;

	unsigned int partNum = 0;
	if (start > MBR_MIN_PART_SIZE) {
		// There is enough space at the start of the device for a usable partition.
		// This is skipped if the whole device is good (start == 0).
		writeEntry(mbr, partNum++, 0, start, MBR_PTYPE_GOOD);
	}
	if ((start != 0) && (end != 0)) {
		// There is a bad section in the middle
		writeEntry(mbr, partNum++, start, end, MBR_PTYPE_BAD);
	}
	if (end < num - MBR_MIN_PART_SIZE) {
		// There is enough space at the end of the device for a usable partition,
		// or the whole device is good.
		writeEntry(mbr, partNum++, end, num, MBR_PTYPE_GOOD);
	}

	// BIOS boot signature
	mbr[0x1FE] = 0x55;
	mbr[0x1FF] = 0xAA;

	// Write out the data
	this->seek(0);
	this->write(mbr, MBR_LEN);
	return;
}
