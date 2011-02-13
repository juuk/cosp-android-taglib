/***************************************************************************
    copyright            : (C) 2002 - 2008 by Scott Wheeler
    email                : wheeler@kde.org
 ***************************************************************************/

/***************************************************************************
 *   This library is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License version   *
 *   2.1 as published by the Free Software Foundation.                     *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful, but   *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA         *
 *   02110-1301  USA                                                       *
 *                                                                         *
 *   Alternatively, this file is available under the Mozilla Public        *
 *   License Version 1.1.  You may obtain a copy of the License at         *
 *   http://www.mozilla.org/MPL/                                           *
 ***************************************************************************/

#include <tbytevector.h>
#include <tdebug.h>
#include <tstring.h>

#include "rifffile.h"
#include <vector>

using namespace TagLib;

class RIFF::File::FilePrivate
{
public:
  FilePrivate() :
    endianness(BigEndian),
    size(0)
  {

  }
  Endianness endianness;
  ByteVector type;
  uint size;
  ByteVector format;

  std::vector<ByteVector> chunkNames;
  std::vector<uint> chunkOffsets;
  std::vector<uint> chunkSizes;
  std::vector<char> chunkPadding;
};

////////////////////////////////////////////////////////////////////////////////
// public members
////////////////////////////////////////////////////////////////////////////////

RIFF::File::~File()
{
  delete d;
}

////////////////////////////////////////////////////////////////////////////////
// protected members
////////////////////////////////////////////////////////////////////////////////

RIFF::File::File(FileName file, Endianness endianness) : TagLib::File(file)
{
  d = new FilePrivate;
  d->endianness = endianness;

  if(isOpen())
    read();
}

TagLib::uint RIFF::File::riffSize() const
{
  return d->size;
}

TagLib::uint RIFF::File::chunkCount() const
{
  return d->chunkNames.size();
}

TagLib::uint RIFF::File::chunkDataSize(uint i) const
{
  return d->chunkSizes[i];
}

TagLib::uint RIFF::File::chunkOffset(uint i) const
{
  return d->chunkOffsets[i];
}

TagLib::uint RIFF::File::chunkPadding(uint i) const
{
  return d->chunkPadding[i];
}

ByteVector RIFF::File::chunkName(uint i) const
{
  if(i >= chunkCount())
    return ByteVector::null;

  return d->chunkNames[i];
}

ByteVector RIFF::File::chunkData(uint i)
{
  if(i >= chunkCount())
    return ByteVector::null;

  // Offset for the first subchunk's data

  long begin = 12 + 8;

  for(uint it = 0; it < i; it++)
    begin += 8 + d->chunkSizes[it] + d->chunkPadding[it];

  seek(begin);

  return readBlock(d->chunkSizes[i]);
}

void RIFF::File::setChunkData(const ByteVector &name, const ByteVector &data)
{
  if(d->chunkNames.size() == 0) {
    debug("RIFF::File::setChunkData - No valid chunks found.");
    return;
  }

  for(uint i = 0; i < d->chunkNames.size(); i++) {
    if(d->chunkNames[i] == name) {

      // First we update the global size

      d->size += ((data.size() + 1) & ~1) - (d->chunkSizes[i] + d->chunkPadding[i]);
      insert(ByteVector::fromUInt(d->size, d->endianness == BigEndian), 4, 4);

      // Now update the specific chunk

      writeChunk(name, data, d->chunkOffsets[i] - 8, d->chunkSizes[i] + d->chunkPadding[i] + 8);

      d->chunkSizes[i] = data.size();
      d->chunkPadding[i] = (data.size() & 0x01) ? 1 : 0;

      // Now update the internal offsets

      for(i++; i < d->chunkNames.size(); i++)
        d->chunkOffsets[i] = d->chunkOffsets[i-1] + 8 + d->chunkSizes[i-1] + d->chunkPadding[i-1];

      return;
    }
  }

  // Couldn't find an existing chunk, so let's create a new one.

  uint i = d->chunkNames.size() - 1;
  ulong offset = d->chunkOffsets[i] + d->chunkSizes[i];

  // First we update the global size

  d->size += (offset & 1) + data.size() + 8;
  insert(ByteVector::fromUInt(d->size, d->endianness == BigEndian), 4, 4);

  // Now add the chunk to the file

  writeChunk(name, data, offset, std::max(ulong(0), length() - offset), (offset & 1) ? 1 : 0);

  // And update our internal structure

  if (offset & 1) {
    d->chunkPadding[i] = 1;
    offset++;
  }
  d->chunkNames.push_back(name);
  d->chunkSizes.push_back(data.size());
  d->chunkOffsets.push_back(offset + 8);
  d->chunkPadding.push_back((data.size() & 0x01) ? 1 : 0);
}

////////////////////////////////////////////////////////////////////////////////
// private members
////////////////////////////////////////////////////////////////////////////////

void RIFF::File::read()
{
  bool bigEndian = (d->endianness == BigEndian);

  d->type = readBlock(4);
  d->size = readBlock(4).toUInt(bigEndian);
  d->format = readBlock(4);

  // + 8: chunk header at least, fix for additional junk bytes
  while(tell() + 8 <= length()) {
    ByteVector chunkName = readBlock(4);
    uint chunkSize = readBlock(4).toUInt(bigEndian);

    if(tell() + chunkSize > uint(length())) {
      // something wrong
      break;
    }

    d->chunkNames.push_back(chunkName);
    d->chunkSizes.push_back(chunkSize);

    d->chunkOffsets.push_back(tell());

    seek(chunkSize, Current);

    // check padding
    char paddingSize = 0;
    long uPosNotPadded = tell();
    if((uPosNotPadded & 0x01) != 0) {
      ByteVector iByte = readBlock(1);
      if((iByte.size() != 1) || (iByte[0] != 0)) {
        // not well formed, re-seek
        seek(uPosNotPadded, Beginning);
      }
      else {
        paddingSize = 1;
      }
    }
    d->chunkPadding.push_back(paddingSize);

  }
}

void RIFF::File::writeChunk(const ByteVector &name, const ByteVector &data,
                            ulong offset, ulong replace, uint leadingPadding)
{
  ByteVector combined;
  if(leadingPadding) {
    combined.append(ByteVector(leadingPadding, '\x00'));
  }
  combined.append(name);
  combined.append(ByteVector::fromUInt(data.size(), d->endianness == BigEndian));
  combined.append(data);
  if((data.size() & 0x01) != 0) {
    combined.append('\x00');
  }
  insert(combined, offset, replace);
}
