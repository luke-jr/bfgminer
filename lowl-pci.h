#ifndef BFG_LOWL_PCI_H
#define BFG_LOWL_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct lowl_pci_handle;

struct _lowl_pci_config {
	int bar;
	size_t sz;
	int mode;
};
extern struct lowl_pci_handle *lowl_pci_open(const char *, const struct _lowl_pci_config *);
#define LP_BARINFO(...)  (struct _lowl_pci_config[]){__VA_ARGS__ { .bar = -1 }}
#define LP_BAR(barno, size, mode)  {barno, size, mode}
extern void lowl_pci_close(struct lowl_pci_handle *);

// Don't assume buf is used in any specific way! Memory returned may be mmap'd (and thus change after call)
extern const uint32_t *lowl_pci_get_words(struct lowl_pci_handle *, void *buf, size_t words, int bar, off_t);
extern bool lowl_pci_set_words(struct lowl_pci_handle *, const uint32_t *, size_t, int bar, off_t);
// buf passed to lowl_pci_get_data must have at least LOWL_PCI_GET_DATA_PADDING bytes more than size to read
#define LOWL_PCI_GET_DATA_PADDING 6
extern const void *lowl_pci_get_data(struct lowl_pci_handle *, void *buf, size_t, int bar, off_t);
extern bool lowl_pci_set_data(struct lowl_pci_handle *, const void *, size_t, int bar, off_t);

static inline
uint32_t lowl_pci_get_word(struct lowl_pci_handle * const lph, const int bar, const off_t offset)
{
	uint32_t buf[1];
	const uint32_t * const p = lowl_pci_get_words(lph, buf, 1, bar, offset);
	if (!p)
		return 0;
	return *p;
}

static inline
bool lowl_pci_set_word(struct lowl_pci_handle * const lph, const int bar, const off_t offset, const uint32_t val)
{
	return lowl_pci_set_words(lph, &val, 1, bar, offset);
}

static inline
bool lowl_pci_read_words(struct lowl_pci_handle * const lph, void * const buf, const size_t words, const int bar, const off_t offset)
{
	const void * const p = lowl_pci_get_words(lph, buf, words, bar, offset);
	if (!p)
		return false;
	if (buf != p)
		memmove(buf, p, words * 4);
	return true;
}

static inline
bool lowl_pci_read_data(struct lowl_pci_handle * const lph, void * const buf, const size_t sz, const int bar, const off_t offset)
{
	const void * const p = lowl_pci_get_data(lph, buf, sz, bar, offset);
	if (!p)
		return false;
	if (buf != p)
		memmove(buf, p, sz);
	return true;
}

#endif
