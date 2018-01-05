#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdlib.h>
#include <elfio/elfio.hpp>
#include "Memory.hpp"

using namespace WdRiscv;


bool
Memory::loadHexFile(const std::string& fileName)
{
  std::ifstream input(fileName);

  if (not input.good())
    {
      std::cerr << "Failed to open hex-file '" << fileName << "' for input\n";
      return false;
    }

  size_t address = 0, errors = 0;

  std::string line;

  for (unsigned lineNum = 0; std::getline(input, line); ++lineNum)
    {
      if (line.empty())
	continue;

      if (line[0] == '@')
	{
	  if (line.size() == 1)
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid hexadecimal address: " << line << '\n';
	      errors++;
	      continue;
	    }
	  char* end = nullptr;
	  address = std::strtoull(line.c_str() + 1, &end, 16);
	  if (end and *end and *end != ' ')
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid hexadecimal address: " << line << '\n';
	      errors++;
	    }
	  continue;
	}

      std::istringstream iss(line);
      uint32_t value;
      while (iss)
	{
	  iss >> std::hex >> value;
	  if (iss.eof())
	    break;
	  if (not iss.good())
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid data: " << line << '\n';
	      errors++;
	      break;
	    }
	  if (value > 0xff)
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid value: " << std::hex << value << '\n';
	      errors++;
	    }
	  if (address < mem_.size())
	    {
	      if (not errors)
		mem_[address++] = value;
	    }
	  else
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Address out of bounds: " << std::hex << address
			<< '\n';
	      errors++;
	      break;
	    }
	}

      if (iss.bad())
	{
	  std::cerr << "File " << fileName << ", Line " << lineNum << ": "
		    << "Failed to parse data line: " << line << '\n';
	  errors++;
	}
    }

  return errors == 0;
}


bool
Memory::loadElfFile(const std::string& fileName, size_t& entryPoint,
		    size_t& exitPoint)
{
  entryPoint = 0;

  ELFIO::elfio reader;

  if (not reader.load(fileName))
    {
      std::cerr << "Failed to load ELF file " << fileName << '\n';
      return false;
    }

  if (reader.get_class() != ELFCLASS32)
    {
      std::cerr << "Ony 32-bit ELF is currently supported\n";
      return false;
    }

  if (reader.get_encoding() != ELFDATA2LSB)
    {
      std::cerr << "Only little-endian ELF is currently supported\n";
      return false;
    }

  if (reader.get_machine() != EM_RISCV)
    {
      std::cerr << "Warning: non-riscv ELF file\n";
    }

  // Copy loadable ELF segments into memory.
  size_t maxEnd = 0;  // Largest end address of a segment.
  unsigned loadedSegs = 0, errors = 0;
  for (int segIx = 0; segIx < reader.segments.size(); ++segIx)
    {
      const ELFIO::segment* seg = reader.segments[segIx];
      ELFIO::Elf64_Addr vaddr = seg->get_virtual_address();
      ELFIO::Elf_Xword size = seg->get_file_size(); // Size in file.
      const char* data = seg->get_data();
      if (seg->get_type() == PT_LOAD)
	{
	  if (vaddr + size >= mem_.size())
	    {
	      std::cerr << "End of ELF segment " << segIx << "("
			<< (vaddr + size)
			<< ") is beyond end of simulated meomry ("
			<< mem_.size() - 1 << ")\n";
	      errors++;
	    }
	  else
	    {
	      for (size_t i = 0; i < size; ++i)
		mem_.at(vaddr + i) = data[i];
	      loadedSegs++;
	      maxEnd = std::max(maxEnd, size_t(vaddr) + size_t(size));
	    }
	}
    }

  // Identify "_finish" symbol.
  bool hasFinish = false;
  size_t finish = 0;
  auto secCount = reader.sections.size();
  for (int secIx = 0; secIx < secCount and not hasFinish; ++secIx)
    {
      auto sec = reader.sections[secIx];
      if (sec->get_type() != SHT_SYMTAB)
	continue;

      const ELFIO::symbol_section_accessor symAccesor(reader, sec);
      ELFIO::Elf64_Addr address = 0;
      ELFIO::Elf_Xword size = 0;
      unsigned char bind, type, other;
      ELFIO::Elf_Half index = 0;

      // Finding symbol by name does not work. Walk all the symbols.
      ELFIO::Elf_Xword symCount = symAccesor.get_symbols_num();
      for (ELFIO::Elf_Xword symIx = 0; symIx < symCount; ++symIx)
	{
	  std::string name;
	  if (symAccesor.get_symbol(symIx, name, address, size, bind, type, index, other))
	    {
	      if (name == "_finish")
		{
		  finish = address;
		  hasFinish = true;
		  break;
		}
	    }
	}
    }

  if (loadedSegs == 0)
    {
      std::cerr << "No loadable segment in ELF file\n";
      errors++;
    }


  // Get the program entry point.
  if (not errors)
    {
      entryPoint = reader.get_entry();
      exitPoint = hasFinish ? finish : maxEnd;
    }

  return errors == 0;
}


void
Memory::resize(size_t newSize, uint8_t value)
{
  mem_.resize(newSize, value);
}


void
Memory::copy(const Memory& other)
{
  size_t n = std::min(mem_.size(), other.mem_.size());
  for (size_t i = 0; i < n; ++i)
    mem_.at(i) = other.mem_.at(i);
}
