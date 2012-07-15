/**
 * @file  device.hpp
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

#ifndef DEVICE_HPP_
#define DEVICE_HPP_

#include <stdint.h>
#include "error.hpp"

/// Data type used to store block numbers.
typedef unsigned long long block_t;

class Device
{
	public:
		virtual ~Device()
			throw ();

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
		virtual block_t size()
			throw (error) = 0;

		/// Change the current seek position.
		virtual void seek(block_t off)
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

		/// Write out a partition table, screening off any bad areas.
		/**
		 * @param firstBad
		 *   Offset, in bytes, of the first bad byte.
		 *
		 * @param lastBad
		 *   Offset, in bytes, of the last bad byte.  The byte following this one
		 *   is good (assuming there is a byte following this one.)
		 *
		 * @param size
		 *   Size of the device, in bytes.
		 */
		void writePartitionTable(block_t firstBad, block_t lastBad, block_t size)
			throw (error);
};

#endif // DEVICE_HPP_
