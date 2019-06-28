#ifndef COUNTINGBLOOM_H
#define COUNTINGBLOOM_H

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

using namespace std;

// Forward declaraions.
template<typename T>
class CountingBloomFilter;

// Method declarations.
template<typename T>
std::ostream&
operator<<(std::ostream&, const CountingBloomFilter<T>&);

template<typename T>
class CountingBloomFilter
{
  public:
	CountingBloomFilter()
	  : m_filter(nullptr)
	  , m_size(0)
	  , m_sizeInBytes(0)
	  , m_hashNum(0)
	  , m_kmerSize(0)
	  , m_dFPR(0)
	  , m_nEntry(0)
	  , m_tEntry(0)
	  , m_countThreshold(0)
	{}
	CountingBloomFilter(size_t sz, unsigned hashNum, unsigned kmerSize, unsigned countThreshold)
	  : m_filter(new T[sz])
	  , m_size(sz)
	  , m_sizeInBytes(sz * sizeof(T))
	  , m_hashNum(hashNum)
	  , m_kmerSize(kmerSize)
	  , m_dFPR(0)
	  , m_nEntry(0)
	  , m_tEntry(0)
	  , m_countThreshold(countThreshold)
	{
		std::memset(m_filter, 0, m_sizeInBytes);
	}
	CountingBloomFilter(const string& path, unsigned countThreshold);
	~CountingBloomFilter() { delete[] m_filter; }
	T operator[](size_t i) { return m_filter[i]; }
	template<typename U>
	T minCount(const U& hashes) const
	{
		T min = m_filter[hashes[0] % m_size];
		for (size_t i = 1; i < m_hashNum; ++i) {
			size_t pos = hashes[i] % m_size;
			if (m_filter[pos] < min)
				min = m_filter[pos];
		}
		return min;
	}
	template<typename U>
	bool contains(const U& hashes) const;
	template<typename U>
	void insert(const U& hashes);
	template<typename U>
	bool insertAndCheck(const U& hashes);
	template<typename U>
	void incrementMin(const U& hashes);
	template<typename U>
	void incrementAll(const U& hashes);
	unsigned getKmerSize(void) const { return m_kmerSize; };
	unsigned getHashNum(void) const { return m_hashNum; };
	unsigned threshold(void) const { return m_countThreshold; };
	size_t size(void) const { return m_size; };
	size_t sizeInBytes(void) const { return m_sizeInBytes; };
	size_t popCount() const;
	size_t filtered_popcount() const;
	double FPR(void) const;
	double filtered_FPR(void) const;

	// Serialization interface
	// When modifiying the header, never remove any fields.
	// Always append to the end of the struct.
	// If there are unused fields, you may rename them,
	// but never change the type or delete the field.
	struct FileHeader
	{
		char magic[8];
		uint32_t hlen;
		uint64_t size;
		uint32_t nhash;
		uint32_t kmer;
		double dFPR;     // unused
		uint64_t nEntry; // unused
		uint64_t tEntry; // unsed
		uint32_t version;
		uint32_t bitsPerCounter;
	};
	void readHeader(FILE* file);
	void readFilter(const string& path);
	void writeHeader(std::ostream& out) const;
	void writeFilter(string const& path) const;
	friend std::ostream& operator<<<>(std::ostream&, const CountingBloomFilter&);

  private:
	// m_filter             : An array of elements of type T; the bit-array or
	//                        filter.
	// m_size               : Size of bloom filter (size of m_filter array).
	// m_sizeInBytes        : Size of the bloom filter in bytes, that is,
	//                        (m_size * sizeof(T)).
	// m_hashNum            : Number of hash functions.
	// m_kmerSize           : Size of a k-mer.
	// m_dFPR               : Unused variable kept for compatability reasons.
	// m_nEntry             : Unused variable kept for compatability reasons.
	// m_tEntry             : Unused variable kept for compatability reasons.
	// BloomFilter_VERSION  : Size of a k-mer.
	// m_countThreshold     : A count greater or equal to this threshold
	//                        establishes existence of an element in the filter.
	// m_bitsPerCounter     : Number of bits per counter.

	T* m_filter;
	size_t m_size;
	size_t m_sizeInBytes;
	unsigned m_hashNum;
	unsigned m_kmerSize;
	double m_dFPR;     // unused
	uint64_t m_nEntry; // unused
	uint64_t m_tEntry; // unused
	static const uint32_t BloomFilter_VERSION = 2;
	unsigned m_countThreshold;
	unsigned m_bitsPerCounter = 8;
};

// Method definitions

// Bloom filter operations

/*
  Use of atomic increments in incrementMin() and incrementAll():

  A atomic compare-and-swap (CAS) operation increments m_filter[pos]. The CAS
  operation takes a memory location and a value that the caller believes the
  location currently stores. If the memory location still holds that value when
  the atomic compare-and-swap executes, then a new value is stored and 'true'
  returned; otherwise, memory is left unchanged and 'false' returned.

  The value of m_filter[pos] may be changed by another thread between a read from
  that memory location and a write to it. The CAS operation is called in a loop
  until it succeeds, which ensures that a write does not happen if some other
  thread has incremented the value between this thread's read and write.

  Note that CAS operations suffer from the ABA problem.
*/

// Of the m_hashNum counters, increment all the minimum values.
template<typename T>
template<typename U>
inline void
CountingBloomFilter<T>::incrementMin(const U& hashes)
{
	// update flag to track if increment is done on at least one counter
	bool updateDone = false;
	T newVal;
	T minVal = minCount(hashes);
	while (!updateDone) {
		// Simple check to deal with overflowhttps://en.wikipedia.org/wiki/Magic_string
		newVal = minVal + 1;
		if (minVal > newVal) {
			return;
		}
		for (size_t i = 0; i < m_hashNum; ++i) {
			if (__sync_bool_compare_and_swap(&m_filter[hashes[i] % m_size], minVal, newVal)) {
				updateDone = true;
			}
		}
		// Recalculate minval because if increment fails, it needs a new minval to use and
		// if it doesnt hava a new one, the while loop runs forever.
		if (!updateDone) {
			minVal = minCount(hashes);
		}
	}
	return;
}

// Increment all the m_hashNum counters.
template<typename T>
template<typename U>
inline void
CountingBloomFilter<T>::incrementAll(const U& hashes)
{
	T currentVal, newVal;
	for (size_t i = 0; i < m_hashNum; ++i) {
		size_t pos = hashes[i] % m_size;
		do {
			currentVal = m_filter[pos];
			newVal = currentVal + 1;
			if (newVal < currentVal)
				break;
		} while (!__sync_bool_compare_and_swap(&m_filter[pos], currentVal, newVal));
	}
}

// Check if an element exists. If the minimum count at the m_hashNum positions
// of m_filter is more than or equal to a predefined count threshold, then the
// element is said to be present in the Bloom filter. count() therefore returns
// true when this condition is satisfied, or else, false.

template<typename T>
template<typename U>
inline bool
CountingBloomFilter<T>::contains(const U& hashes) const
{
	return minCount(hashes) >= m_countThreshold;
}

template<typename T>
template<typename U>
inline void
CountingBloomFilter<T>::insert(const U& hashes)
{
	incrementMin(hashes);
}

template<typename T>
template<typename U>
inline bool
CountingBloomFilter<T>::insertAndCheck(const U& hashes)
{
	bool found = contains(hashes);
	incrementMin(hashes);
	return found;
}

/* Count the number of non-zero counters. */
template<typename T>
size_t
CountingBloomFilter<T>::popCount() const
{
	size_t count = 0;
	for (size_t i = 0; i < m_size; ++i) {
		if (m_filter[i] != 0)
			++count;
	}
	return count;
}

/* Count the number of above threshold counters. */
template<typename T>
size_t
CountingBloomFilter<T>::filtered_popcount() const
{
	size_t count = 0;
	for (size_t i = 0; i < m_size; ++i) {
		if (m_filter[i] >= m_countThreshold)
			++count;
	}
	return count;
}

template<typename T>
double
CountingBloomFilter<T>::FPR(void) const
{
	return std::pow((double)popCount() / (double)m_size, m_hashNum);
}

template<typename T>
double
CountingBloomFilter<T>::filtered_FPR(void) const
{
	return std::pow((double)filtered_popcount() / (double)m_size, m_hashNum);
}

// Serialization interface.
template<typename T>
CountingBloomFilter<T>::CountingBloomFilter(const string& path, unsigned countThreshold)
  : m_dFPR(0)
  , m_nEntry(0)
  , m_tEntry(0)
  , m_countThreshold(countThreshold)
{
	readFilter(path);
}

template<typename T>
void
CountingBloomFilter<T>::readFilter(const string& path)
{
	FILE* fp;
	if ((fp = fopen(path.c_str(), "rb")) == nullptr) {
		cerr << "ERROR: Failed to open file: " << path << "\n";
		exit(EXIT_FAILURE);
	}
	readHeader(fp);
	long int lCurPos = ftell(fp);
	fseek(fp, 0, 2);
	size_t arraySizeOnDisk = ftell(fp) - sizeof(struct FileHeader);
	fseek(fp, lCurPos, 0);
	if (arraySizeOnDisk != m_sizeInBytes) {
		cerr << "ERROR: File size of " << path << " (" << arraySizeOnDisk << " bytes), "
		     << "does not match size read from its header (" << m_sizeInBytes << " bytes).\n";
		exit(EXIT_FAILURE);
	}

	size_t nread = fread(m_filter, arraySizeOnDisk, 1, fp);
	if (nread != 1 && fclose(fp) != 0) {
		cerr << "ERROR: The bit array could not be read from the file: " << path << "\n";
		exit(EXIT_FAILURE);
	}
}

template<typename T>
void
CountingBloomFilter<T>::readHeader(FILE* fp)
{
	FileHeader header;
	if (fread(&header, sizeof(struct FileHeader), 1, fp) != 1) {
		cerr << "Failed to read header\n";
		exit(EXIT_FAILURE);
	}
	if (header.hlen != sizeof(FileHeader)) {
		cerr << "Bloom Filter header length: " << header.hlen
		     << " does not match expected length: " << sizeof(FileHeader)
		     << " (likely version mismatch)" << endl;
		exit(EXIT_FAILURE);
	}
	char magic[9];
	memcpy(magic, header.magic, 8);
	magic[8] = '\0';
	if (strcmp(magic, "BLOOMCOU")) {
		cerr << "Bloom Filter type does not match" << endl;
		exit(EXIT_FAILURE);
	}
	if (header.version != BloomFilter_VERSION) {
		cerr << "Bloom Filter version does not match: " << header.version
		     << " expected: " << BloomFilter_VERSION << endl;
		exit(EXIT_FAILURE);
	}
	m_size = header.size;
	m_hashNum = header.nhash;
	m_kmerSize = header.kmer;
	m_sizeInBytes = m_size * sizeof(T);
	m_filter = new T[m_size]();
	m_bitsPerCounter = header.bitsPerCounter;
}

template<typename T>
void
CountingBloomFilter<T>::writeFilter(string const& path) const
{
	ofstream ofs(path.c_str(), ios::out | ios::binary);
	cerr << "Writing a " << m_sizeInBytes << " byte filter to a file on disk.\n";
	ofs << *this;
	ofs.close();
	assert(ofs);
}

template<typename T>
void
CountingBloomFilter<T>::writeHeader(std::ostream& out) const
{
	FileHeader header;
	memcpy(header.magic, "BLOOMCOU", 8);
	header.hlen = sizeof(struct FileHeader);
	header.size = m_size;
	header.nhash = m_hashNum;
	header.kmer = m_kmerSize;
	header.version = BloomFilter_VERSION;
	header.bitsPerCounter = m_bitsPerCounter;
	out.write(reinterpret_cast<char*>(&header), sizeof(struct FileHeader));
	assert(out);
}

// Serialize the bloom filter to a C++ stream */
template<typename T>
std::ostream&
operator<<(std::ostream& os, const CountingBloomFilter<T>& cbf)
{
	assert(os);
	cbf.writeHeader(os);
	assert(os);
	os.write(reinterpret_cast<char*>(cbf.m_filter), cbf.m_sizeInBytes);
	assert(os);
	return os;
}

#endif // COUNTINGBLOOM_H
