/*
 * pico_littlefs — Convenience Wrapper Implementation
 */

#include "pico_littlefs.h"

int pico_littlefs_format(lfs_t *lfs, const struct lfs_config *cfg)
{
	return lfs_format(lfs, cfg);
}

int pico_littlefs_mount(lfs_t *lfs, const struct lfs_config *cfg)
{
	return lfs_mount(lfs, cfg);
}

int pico_littlefs_unmount(lfs_t *lfs)
{
	return lfs_unmount(lfs);
}
