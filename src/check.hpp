/**
 * @file  check.hpp
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

#ifndef CHECK_HPP_
#define CHECK_HPP_

#include "device.hpp"
#include "error.hpp"

/// Size of each read and write operation.  Should match the underlying device.
#define DATA_BLOCK_SIZE 4096

/// Abort when getting read errors continously for this many seconds
#define MAX_READ_ERROR_TIME 15

class CheckCallback
{
	public:
		virtual ~CheckCallback()
			throw ();

		/// Ask the user whether they want to resume a previous write operation.
		/**
		 * @return true to resume, false to start over.
		 */
		virtual bool resumeWrite()
			throw () = 0;

		/// The write operation is beginning.
		/**
		 * @param startBlock
		 *   Block number of first blocked checked.  Will be non-zero if resuming
		 *   a previous write operation.
		 *
		 * @param numBlocks
		 *   Number of blocks on storage device.
		 */
		virtual void writeStart(block_t startBlock, block_t numBlocks)
			throw () = 0;

		/// Update the user on how the write operation is going.
		/**
		 * @param b
		 *   Current block number.  Will always be <= startBlock passed to
		 *   writeStart().
		 */
		virtual void writeProgress(block_t b)
			throw () = 0;

		/// The write operation has completed.
		virtual void writeFinish()
			throw () = 0;

		/// The read operation is beginning.
		/**
		 * @param startBlock
		 *   Block number of first blocked checked.  Will be non-zero if resuming
		 *   a previous read operation.
		 *
		 * @param numBlocks
		 *   Number of blocks on storage device.
		 */
		virtual void readStart(block_t startBlock, block_t numBlocks)
			throw () = 0;

		/// Update the user on how the read operation is going.
		/**
		 * @param b
		 *   Current block number.  Will always be <= startBlock passed to
		 *   readStart().
		 *
		 * @param fail
		 *   True if this block couldn't be read due to an I/O error.  False if it
		 *   could be read, even if it was corrupted.
		 *
		 * @return true to keep going, false to abort.
		 */
		virtual bool readProgress(block_t b, bool fail)
			throw () = 0;

		/// The read operation has completed.
		virtual void readFinish()
			throw () = 0;

		/// The check has finished, and here are the results.
		virtual void checkComplete()
			throw () = 0;
};

class Check
{
	public:
		Check(Device *dev, CheckCallback *cb)
			throw (error);

		~Check()
			throw ();

		/// Open the given device.
		void use(Device *dev)
			throw (error);

		/// Write out verification data to the device.
		void write()
			throw (error);

		/// Read back the verification data from the device.
		void read()
			throw (error);

	protected:
		Device *dev;        ///< Device being examined
		CheckCallback *cb;  ///< Who to notify about events
		block_t numBlocks; ///< Size of device, in blocks
};

#endif // CHECK_HPP_
