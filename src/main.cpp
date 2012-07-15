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

#include "error.hpp"
#include "device.hpp"
#include "check.hpp"

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

class POSIXDevice: virtual public Device
{
	public:
		POSIXDevice()
			: fd(-1)
		{
		}

		virtual ~POSIXDevice()
			throw ()
		{
			try {
				if (fd >= 0) this->close();
			} catch (POSIXError) {
			}
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

class ConsoleUI: virtual public CheckCallback
{
	public:
		virtual ~ConsoleUI()
			throw ()
		{
		}

		virtual bool resumeWrite()
			throw ()
		{
			std::cout <<
				"\nThis device appears to be in the process of being checked.  Possibly a\n"
				"previous run was aborted early.  You can resume this check or start over.\n"
				"Resume (Y/N)? " << std::flush;
			char key = 'n';
			std::cin >> key;
			if ((key == 'y') || (key == 'Y')) {
				return true;
			}
			return false;
		}

		virtual void writeStart(block_t startBlock, block_t numBlocks)
			throw ()
		{
			this->startBlock = startBlock;
			this->numBlocks = numBlocks;
			gettimeofday(&this->tmStart, NULL);
			return;
		}

		virtual void writeProgress(block_t b)
			throw ()
		{
			std::cout << "\rWriting to block " << b
				<< " [" << b * 100 / (this->numBlocks - 1) << "%] ";
			if (b > 0) {
				struct timeval tmNow;
				gettimeofday(&tmNow, NULL);
				time_t duration = tmNow.tv_sec - this->tmStart.tv_sec;
				unsigned long remTime = duration * (this->numBlocks - 1 - b) / (b - this->startBlock);
				unsigned int s = remTime % 60;
				unsigned int m = (remTime / 60) % 60;
				unsigned int h = remTime / 3600;
				std::cout << "ETA "
					<< std::setw(2) << std::setfill('0') << h << ':'
					<< std::setw(2) << std::setfill('0') << m << ':'
					<< std::setw(2) << std::setfill('0') << s;
				if (duration > 0) {
					std::cout << ' '
						<< ((b - this->startBlock) * (DATA_BLOCK_SIZE / 1024)) / duration
						<< "kB/sec";
				}
				std::cout << std::flush;
			}
			return;
		}

		virtual void writeFinish()
			throw ()
		{
			std::cout << "\n";
			return;
		}

		virtual void readStart(block_t startBlock, block_t numBlocks)
			throw ()
		{
			this->startBlock = startBlock;
			this->numBlocks = numBlocks;
			gettimeofday(&this->tmStart, NULL);
			this->lastDuration = 0;
			this->firstReadError = 0;
			return;
		}

		virtual bool readProgress(block_t b, bool fail)
			throw ()
		{
			std::cout << "\rReading from block " << b
				<< " [" << b * 100 / (this->numBlocks - 1) << "%] ";
			struct timeval tmNow;
			gettimeofday(&tmNow, NULL);
			time_t duration = tmNow.tv_sec - this->tmStart.tv_sec;
			if ((b > 0) && (duration != lastDuration)) {
				lastDuration = duration;
				unsigned long remTime = duration * (this->numBlocks - 1 - b) / (b - this->startBlock);
				unsigned int s = remTime % 60;
				unsigned int m = (remTime / 60) % 60;
				unsigned int h = remTime / 3600;
				std::cout << "ETA "
					<< std::setw(2) << std::setfill('0') << h << ':'
					<< std::setw(2) << std::setfill('0') << m << ':'
					<< std::setw(2) << std::setfill('0') << s;
				if (duration > 0) {
					std::cout << ' '
						<< ((b - this->startBlock) * (DATA_BLOCK_SIZE / 1024)) / duration
						<< "kB/sec";
				}
				std::cout << std::flush;
			}
			if (fail) {
				if (this->firstReadError == 0) {
					this->firstReadError = duration;
				} else if (duration - this->firstReadError > MAX_READ_ERROR_TIME) {
					std::cout << "\nRead bad blocks continuously for "
						<< MAX_READ_ERROR_TIME << " seconds, aborting.\n";/*"Last error was: "
						<< e.what();*/
					// TODO: Jump to end and read backwards
					// TODO: Warn that this can be caused by a low-quality card reader and to try in another one.
					return false;
				}
			} else {
				this->firstReadError = 0; // got a good block, reset the error count
			}
			return true;
		}

		virtual void readFinish()
			throw ()
		{
			std::cout << "\n";
			return;
		}

		virtual void checkComplete()
			throw ()
		{
			return;
		}

	protected:
		struct timeval tmStart;
		time_t lastDuration;
		time_t firstReadError; ///< Time of the first error in the current run of errors
		block_t startBlock;
		block_t numBlocks;
};

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

	ConsoleUI *ui = new ConsoleUI();
	Check *chk = new Check(dev, ui);
	chk->write();
	std::cout << "\n";
	chk->read();
	std::cout << "\n";

	delete chk;
	delete ui;
	delete dev;

	return 0;
}
