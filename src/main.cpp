/**
 * @file  main.cpp
 * @brief Entry point for scanflash.
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

#include <iostream>
#include <iomanip>
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <stropts.h>
#include <linux/fs.h>
#include <math.h>

#include "error.hpp"

#define DATA_BLOCK_SIZE 4096

/// Abort when getting read errors continously for this many seconds
#define MAX_READ_ERROR_TIME 15

enum ReturnCodes {
	RET_DEVICE_OK     = 0, ///< Test completed successfully, flash drive good
	RET_BAD_ARGS      = 1, ///< No device name given
	RET_NO_OPEN       = 2, ///< Unable to open the device
	RET_ABORTED       = 3, ///< User aborted the test
	RET_DEVICE_FAILED = 8, ///< Test completed successfully, flash drive bad
};

class POSIXError: virtual public error
{
	public:
		POSIXError(int num)
			throw ()
			: error(strerror(num))
		{
		}
};

class Device
{
	public:
		virtual ~Device()
		{
		}

		/// Open the given device.
		virtual void open(const char *path)
			throw (error) = 0;

		/// Close the device handle.
		/**
		 * @post No other functions can be called except for open() and reopen().
		 */
		virtual void close()
			throw (error) = 0;

		/// Re-open the device originally passed to open().
		/**
		 * @pre open() must have been called previously.
		 */
		virtual void reopen()
			throw (error) = 0;

		/// Get the size of the device, in bytes.
		virtual unsigned long long size()
			throw (error) = 0;

		/// Change the current seek position.
		virtual void seek(unsigned long long off)
			throw (error) = 0;

		/// Write some data to the current seek position.
		virtual void write(uint8_t *buf, unsigned int len)
			throw (error) = 0;

		/// Read some data at the current seek position.
		virtual void read(uint8_t *buf, unsigned int len)
			throw (error) = 0;

		/// Ensure all cached data is written to the device.
		virtual void sync()
			throw (error) = 0;
};

class POSIXDevice: virtual public Device
{
	public:
		POSIXDevice()
			: fd(-1)
		{
		}

		virtual ~POSIXDevice()
		{
			if (fd >= 0) this->close();
		}

		virtual void open(const char *path)
			throw (POSIXError)
		{
			this->devPath = path;
			this->reopen();
		}

		virtual void close()
			throw (POSIXError)
		{
			::close(fd);
			this->fd = -1;
		}

		virtual void reopen()
			throw (POSIXError)
		{
			this->fd = ::open(this->devPath.c_str(), O_RDWR | O_SYNC);// | O_DSYNC | O_RSYNC | O_NONBLOCK);
			if (this->fd < 0) throw POSIXError(errno);
		}

		virtual unsigned long long size()
			throw ()
		{
			off64_t len = lseek64(this->fd, 0, SEEK_END);
			unsigned long long amt = len;
			return amt;
		}

		virtual void seek(unsigned long long off)
			throw ()
		{
			lseek64(this->fd, off, SEEK_SET);
			return;
		}

		virtual void write(uint8_t *buf, unsigned int len)
			throw (POSIXError)
		{
			if (::write(this->fd, buf, len) < 0) throw POSIXError(errno);
			return;
		}

		virtual void read(uint8_t *buf, unsigned int len)
			throw (POSIXError)
		{
			if (::read(this->fd, buf, len) < 0) throw POSIXError(errno);
			return;
		}

		virtual void sync()
			throw (POSIXError)
		{
			// Ensure all data is written to the device
			if (fsync(this->fd) < 0) throw POSIXError(errno);
			if (fdatasync(this->fd) < 0) throw POSIXError(errno);
			// Flush all kernel caches, hopefully to avoid reading back the cache
			// instead of from the device.
			if (ioctl(this->fd, BLKFLSBUF, NULL)) {
				throw POSIXError(errno);
			}
			return;
		}

	protected:
		int fd;
		std::string devPath;
};

// Write a code to the block
void prepareBuf(uint8_t *buf, unsigned int len, unsigned long long blockNum)
{
	for (unsigned int i = 0; i < len; i += sizeof(unsigned long long)) {
		memcpy(&buf[i], &blockNum, sizeof(unsigned long long));
	}
	return;
}

int main(int argc, char *argv[])
{
	std::cout << "scanflash - scan memory cards to detect fakes\n"
		"Copyright (C) 2012 Adam Nielsen <http://www.shikadi.net/scanflash>\n"
		<< std::endl;

	if (argc != 2) {
		std::cerr << "Use: scanflash <device>" << std::endl;
		return RET_BAD_ARGS;
	}

	Device *dev = new POSIXDevice();
	try {
		dev->open(argv[1]);
	} catch (const error& e) {
		std::cerr << "Unable to open device: " << e.what() << std::endl;
		return RET_NO_OPEN;
	}

	std::cout << "WARNING: All data on " << argv[1] << " will be erased permanently!\n"
		"Are you sure you wish to continue (Y/N)? " << std::flush;

	char key = 'n';
	std::cin >> key;
	if ((key != 'y') && (key != 'Y')) {
		delete dev;
		std::cout << "Aborted.\n";
		return RET_ABORTED;
	}

	unsigned long long len = dev->size();
	unsigned long long numBlocks = len / DATA_BLOCK_SIZE;
	std::cout << "Device is " << len << " bytes in size (" << numBlocks
		<< " blocks of " << DATA_BLOCK_SIZE << ").\n";

	unsigned long long startBlock = 0;

	uint8_t buf[DATA_BLOCK_SIZE], origBuf[DATA_BLOCK_SIZE];
	prepareBuf(origBuf, DATA_BLOCK_SIZE, 0);
	dev->seek(0);
	dev->read(buf, DATA_BLOCK_SIZE);
	if (memcmp(origBuf, buf, DATA_BLOCK_SIZE) == 0) {
		std::cout <<
			"\nThis device appears to be in the process of being checked.  Possibly a\n"
			"previous run was aborted early.  You can resume this check or start over.\n"
			"Resume (Y/N)? " << std::flush;
		key = 'n';
		std::cin >> key;
		if ((key == 'y') || (key == 'Y')) {
			// Figure out where the last write operation was done
			unsigned long long remainingBlocks = numBlocks / 2;
			startBlock = remainingBlocks;
			//while ((nextBlock > 0) && (nextBlock < numBlocks - 2)) {
			unsigned long long numLoops = log2(numBlocks);
			unsigned long long i = 0;
			while (remainingBlocks > 1) {
				std::cout << "\rScanning block " << startBlock
					<< " (" << i << '/' << numLoops << ')' << std::flush;
				i++;
				dev->seek(startBlock * DATA_BLOCK_SIZE);
				dev->read(buf, DATA_BLOCK_SIZE);
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

	struct timeval tmStart, tmNow;
	std::cout << "\n";
	// Write out data to each block
	dev->seek(startBlock * DATA_BLOCK_SIZE);
	gettimeofday(&tmStart, NULL);
	for (unsigned long long b = startBlock; b < numBlocks; b++) {
		if ((b % 256) == 0) {
			std::cout << "\rWriting to block " << b
				<< " [" << b * 100 / numBlocks << "%] ";
			if (b > 0) {
				gettimeofday(&tmNow, NULL);
				time_t duration = tmNow.tv_sec - tmStart.tv_sec;
				unsigned long remTime = duration * (numBlocks - b) / (b - startBlock);
				unsigned int s = remTime % 60;
				unsigned int m = (remTime / 60) % 60;
				unsigned int h = remTime / 3600;
				std::cout << "ETA "
					<< std::setw(2) << std::setfill('0') << h << ':'
					<< std::setw(2) << std::setfill('0') << m << ':'
					<< std::setw(2) << std::setfill('0') << s;
				if (duration > 0) {
					std::cout << ' '
						<< ((b - startBlock) * (DATA_BLOCK_SIZE / 1024)) / duration
						<< "kB/sec";
				}
				std::cout << std::flush;
			}
		}
		prepareBuf(buf, DATA_BLOCK_SIZE, b);
		dev->write(buf, DATA_BLOCK_SIZE);
	}
	std::cout << "\n";

	try {
		dev->sync();
	} catch (const error& e) {
		dev->close();
		std::cout << "\nError flushing device: " << e.what() << "\n"
			"You should remove and reattach the storage device before continuing,\n"
			"to ensure the data that is about to be read is coming from the device\n"
			"itself and not any system caches.  If you continue without reattaching\n"
			"the device, some faults may not be detected.\n"
			"Continue (Y/N)? " << std::flush;
		for (;;) {
			key = 'n';
			std::cin >> key;
			if ((key != 'y') && (key != 'Y')) {
				delete dev;
				std::cout << "Aborted.\n";
				return RET_ABORTED;
			}
			try {
				dev->reopen();
				break;
			} catch (const error& e) {
				std::cout << "Unable to reopen device: " << e.what()
					<< "\nTry again (Y/N)? ";
			}
		}
	}

	bool firstBad = false;
	unsigned long long firstBadBlock, lastBadBlock;

	startBlock = 0;

	// Read data back again
	dev->seek(0);
	gettimeofday(&tmStart, NULL);
	time_t lastDuration = 0;
	time_t firstReadError = 0;
	for (unsigned long long b = startBlock; b < numBlocks; b++) {
		if ((b % 256) == 0) {
			std::cout << "\rReading from block " << b
				<< " [" << b * 100 / numBlocks << "%] ";
			gettimeofday(&tmNow, NULL);
			time_t duration = tmNow.tv_sec - tmStart.tv_sec;
			if ((b > 0) && (duration != lastDuration)) {
				lastDuration = duration;
				unsigned long remTime = duration * (numBlocks - b) / (b - startBlock);
				unsigned int s = remTime % 60;
				unsigned int m = (remTime / 60) % 60;
				unsigned int h = remTime / 3600;
				std::cout << "ETA "
					<< std::setw(2) << std::setfill('0') << h << ':'
					<< std::setw(2) << std::setfill('0') << m << ':'
					<< std::setw(2) << std::setfill('0') << s;
				if (duration > 0) {
					std::cout << ' '
						<< ((b - startBlock) * (DATA_BLOCK_SIZE / 1024)) / duration
						<< "kB/sec";
				}
				std::cout << std::flush;
			}
		}
		prepareBuf(origBuf, DATA_BLOCK_SIZE, b);
		try {
			dev->read(buf, DATA_BLOCK_SIZE);
			if (memcmp(origBuf, buf, DATA_BLOCK_SIZE) != 0) {
				// Data doesn't match, investigate
				if (!firstBad) {
					firstBadBlock = b;
					firstBad = true;
				}
				lastBadBlock = b;
				// Find number read back and mark that as largest suspect block, if it's
				// larger than the current suspect block
			} else { // data is ok
				firstReadError = 0; // got a good block, reset the error count
			}
		} catch (const error& e) {
			gettimeofday(&tmNow, NULL);
			time_t duration = tmNow.tv_sec - tmStart.tv_sec;
			if (firstReadError == 0) {
				firstReadError = duration;
			} else if (duration - firstReadError > MAX_READ_ERROR_TIME) {
				std::cout << "\nRead bad blocks continuously for "
					<< MAX_READ_ERROR_TIME << " seconds, aborting.\nLast error was: "
					<< e.what();
				// TODO: Jump to end and read backwards
				// TODO: Warn that this can be caused by a low-quality card reader and to try in another one.
				break;
			}
			if (!firstBad) {
				firstBadBlock = b;
				firstBad = true;
			}
			lastBadBlock = b;
		}
	}
	std::cout << "\n";

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

	delete dev;
	return 0;
}
