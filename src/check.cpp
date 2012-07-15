/**
 * @file  check.cpp
 * @brief Platform/device independent checking algorithm.
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

#include <iostream> // TEMP
#include <iomanip> // TEMP

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.hpp"

/// Write a code to the block
void prepareBuf(uint8_t *buf, unsigned int len, block_t blockNum)
{
	blockNum++; // avoid block 0 having all zeroes
	for (unsigned int i = 0; i < len; i += sizeof(block_t)) {
		memcpy(&buf[i], &blockNum, sizeof(block_t));
	}
	return;
}

CheckCallback::~CheckCallback()
	throw ()
{
}

Check::Check(Device *dev, CheckCallback *cb)
	throw (error)
	: dev(dev),
	  cb(cb)
{
	block_t len = this->dev->size();
	this->numBlocks = len / DATA_BLOCK_SIZE;
}

Check::~Check()
	throw ()
{
}

void Check::write()
	throw (error)
{
	block_t startBlock = 0;

	uint8_t buf[DATA_BLOCK_SIZE], origBuf[DATA_BLOCK_SIZE];
	prepareBuf(origBuf, DATA_BLOCK_SIZE, 0);
	this->dev->seek(0);
	this->dev->read(buf, DATA_BLOCK_SIZE);
	if (memcmp(origBuf, buf, DATA_BLOCK_SIZE) == 0) {
		// Ask the user if they want to resume
		if (this->cb->resumeWrite()) {
			// Yes, so figure out where the last write operation was done
			block_t remainingBlocks = this->numBlocks / 2;
			startBlock = remainingBlocks;
			//while ((nextBlock > 0) && (nextBlock < numBlocks - 2)) {
			block_t numLoops = log2(numBlocks);
			block_t i = 0;
			while (remainingBlocks > 1) {
				std::cout << "\rScanning block " << startBlock
					<< " (" << i << '/' << numLoops << ')' << std::flush;
				i++;
				this->dev->seek(startBlock * DATA_BLOCK_SIZE);
				this->dev->read(buf, DATA_BLOCK_SIZE);
				prepareBuf(origBuf, DATA_BLOCK_SIZE, startBlock);
				remainingBlocks /= 2;
				if (memcmp(origBuf, buf, DATA_BLOCK_SIZE) == 0) {
					// This block has already been written
					startBlock += remainingBlocks;
				} else {
					// This block hasn't been written yet (or is corrupted)
					startBlock -= remainingBlocks;
				}
			}
			std::cout << "\nResuming write at block " << startBlock << "\n";
		}
	}

	std::cout << "\n";

	// Write out data to each block
	this->dev->seek(startBlock * DATA_BLOCK_SIZE);
	this->cb->writeStart(startBlock, numBlocks);
	for (block_t b = startBlock; b < numBlocks; b++) {
		if ((b % 256) == 0) {
			this->cb->writeProgress(b);
		}
		prepareBuf(buf, DATA_BLOCK_SIZE, b);
		this->dev->write(buf, DATA_BLOCK_SIZE);
	}

	this->cb->writeProgress(numBlocks - 1); // signal 100%
	this->cb->writeFinish();

	try {
		this->dev->sync();
	} catch (const error& e) {
		this->dev->close();
		std::cout << "\nError flushing device: " << e.what() << "\n"
			"You should remove and reattach the storage device before continuing,\n"
			"to ensure the data that is about to be read is coming from the device\n"
			"itself and not any system caches.  If you continue without reattaching\n"
			"the device, some faults may not be detected.\n"
			"Continue (Y/N)? " << std::flush;
		for (;;) {
			char key = 'n';
			std::cin >> key;
			if ((key != 'y') && (key != 'Y')) {
				throw error("Aborted by user");
			}
			try {
				this->dev->reopen();
				break;
			} catch (const error& e) {
				std::cout << "Unable to reopen device: " << e.what()
					<< "\nTry again (Y/N)? ";
			}
		}
	}
	return;
}

void Check::read()
	throw (error)
{
	bool firstBad = false;
	block_t firstBadBlock = 0, lastBadBlock = 0;

	block_t startBlock = 0;
	uint8_t buf[DATA_BLOCK_SIZE], origBuf[DATA_BLOCK_SIZE];

	// Read data back again
	this->dev->seek(0);
	this->cb->readStart(startBlock, numBlocks);
	bool fail = false; // was this block good or bad?
	for (block_t b = startBlock; b < numBlocks; b++) {
		prepareBuf(origBuf, DATA_BLOCK_SIZE, b);
		try {
			this->dev->read(buf, DATA_BLOCK_SIZE);
			fail = false;
			if (memcmp(origBuf, buf, DATA_BLOCK_SIZE) != 0) {
				// Data doesn't match, investigate
				if (!firstBad) {
					firstBadBlock = b;
					firstBad = true;
				}
				lastBadBlock = b;
				// Find number read back and mark that as largest suspect block, if it's
				// larger than the current suspect block
			}
		} catch (const error& e) {
			if (!firstBad) {
				firstBadBlock = b;
				firstBad = true;
			}
			lastBadBlock = b;
			fail = true;
		}
		if (((b % 256) == 0) || fail) {
			if (!this->cb->readProgress(b, fail)) throw error("Verification operation aborted");
		}
	}
	if (!fail) this->cb->readProgress(numBlocks - 1, true); // signal 100%
	this->cb->readFinish();

	// TODO: Last x MB will be wrong if it would be overwritten by earlier data
	//       Of course it could mean there'd be a larger available block at the end of the card...
	if (firstBad) {
		std::cout << "First bad block was at " << firstBadBlock << " (* "
			<< DATA_BLOCK_SIZE << " = byte offset " << firstBadBlock * DATA_BLOCK_SIZE << ")\n"
			<< "  >> First " << firstBadBlock * DATA_BLOCK_SIZE / 1048576 << "MB are good\n"
			<< "Last bad block was at " << lastBadBlock << " (next good byte offset "
			<< (lastBadBlock + 1) * DATA_BLOCK_SIZE << ")\n"
			<< "  >> Last "
			<< (numBlocks - (lastBadBlock + 1)) * DATA_BLOCK_SIZE / 1048576
			<< "MB are good\n"
			<< std::endl;
	} else {
		std::cout << "No bad blocks detected.  This device is 100% functional!"
			<< std::endl;
	}

	// Write out a replacement partition table
	if (firstBad) {
		this->dev->writePartitionTable(
			firstBadBlock * DATA_BLOCK_SIZE,
			(lastBadBlock + 1) * DATA_BLOCK_SIZE - 1,
			this->numBlocks * DATA_BLOCK_SIZE);
	} else {
		this->dev->writePartitionTable(0, 0, this->numBlocks * DATA_BLOCK_SIZE);
	}

	return;
}
