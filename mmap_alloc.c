#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#ifdef MODVERSIONS
#  include <linux/modversions.h>
#endif
#include <asm/io.h>

/*
 * Example of driver that allows a user-space program to mmap a buffer of
 * contiguous non-cached physical memory.
 * Note that pages are contiguous in physical memory but not in virtual memory.
 * Based on the original work made by Martin Frey <frey@scs.ch>.
 *
 * Authors: Claudio Scordino, Bruno Morelli
 */

/* character device structures */
static dev_t mmap_dev;
static struct cdev mmap_cdev;

/* methods of the character device */
static int mmap_open(struct inode *inode, struct file *filp);
static int mmap_release(struct inode *inode, struct file *filp);
static int mmap_mmap(struct file *filp, struct vm_area_struct *vma);

/* the file operations, i.e. all character device methods */
static struct file_operations mmap_fops = {
        .open = mmap_open,
        .release = mmap_release,
        .mmap = mmap_mmap,
        .owner = THIS_MODULE,
};

// length of the two memory areas
#define NPAGES 16
// pointer to the allocated area, rounded up to a page boundary
static int *alloc_area;
// original pointer for allocated area
static void *alloc_ptr;
dma_addr_t dma_handle;

/* character device open method */
static int mmap_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "mmap_alloc: device open\n");
        return 0;
}
/* character device last close method */
static int mmap_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "mmap_alloc: device is being released\n");
        return 0;
}

// helper function, mmap's the allocated area which is physically contiguous
int mmap_kmem(struct file *filp, struct vm_area_struct *vma)
{
        int ret;
        long length = vma->vm_end - vma->vm_start;

        /* check length - do not allow larger mappings than the number of
           pages allocated */
        if (length > NPAGES * PAGE_SIZE)
                return -EIO;
/* #ifdef ARCH_HAS_DMA_MMAP_COHERENT */
	if (vma->vm_pgoff == 0) {
		printk(KERN_INFO "Using dma_mmap_coherent\n");
		ret = dma_mmap_coherent(NULL, vma, alloc_ptr,
					dma_handle, length);
	} else
/* #else */
	{
		printk(KERN_INFO "Using remap_pfn_range\n");
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		vma->vm_flags |= VM_IO;
		printk(KERN_INFO "off=%d\n", vma->vm_pgoff);
	        ret = remap_pfn_range(vma, vma->vm_start,
			      PFN_DOWN(virt_to_phys(bus_to_virt(dma_handle))) +
			      vma->vm_pgoff, length, vma->vm_page_prot);
	}
/* #endif */
        /* map the whole physically contiguous area in one piece */
        if (ret < 0) {
		printk(KERN_ERR "mmap_alloc: remap failed (%d)\n", ret);
		return ret;
        }
        
        return 0;
}

/* character device mmap method */
static int mmap_mmap(struct file *filp, struct vm_area_struct *vma)
{
	printk(KERN_INFO "mmap_alloc: device is being mapped\n");
        return mmap_kmem(filp, vma);
}

/* module initialization - called at module load time */
static int __init mmap_alloc_init(void)
{
        int ret = 0;
        int i;

	/* Allocate not-cached memory area with dma_map_coherent. */
	printk(KERN_INFO "Use dma_alloc_coherent\n");
	alloc_ptr = dma_alloc_coherent (NULL, (NPAGES + 2) * PAGE_SIZE,
					&dma_handle, GFP_KERNEL);

	if (!alloc_ptr) {
                printk(KERN_ERR
		    "mmap_alloc: dma_alloc_coherent error\n");
                ret = -ENOMEM;
                goto out;
        }
	printk(KERN_INFO "mmap_alloc: physical address is %lu\n",
	    dma_handle);
	printk(KERN_INFO "mmap_alloc: bus_to_virt %lu\n",
	    virt_to_phys(bus_to_virt(dma_handle)));

	alloc_area = alloc_ptr;

        /* get the major number of the character device */
        if ((ret = alloc_chrdev_region(&mmap_dev, 0, 1, "mmap_alloc")) < 0) {
                printk(KERN_ERR
		    "mmap_alloc: could not allocate major number for mmap\n");
                goto out_vfree;
        }

        /* initialize the device structure and register the device with the
	 * kernel */
        cdev_init(&mmap_cdev, &mmap_fops);
        if ((ret = cdev_add(&mmap_cdev, mmap_dev, 1)) < 0) {
                printk(KERN_ERR
		    "mmap_alloc: could not allocate chrdev for mmap\n");
                goto out_unalloc_region;
        }

	/* store a pattern in the memory.
	 * the test application will check for it */
        for (i = 0; i < (NPAGES * PAGE_SIZE / sizeof(int)); i += 2) {
                alloc_area[i] = (0xdead << 16) + i;
                alloc_area[i + 1] = (0xbeef << 16) + i;
        }
        
        return ret;
        
  out_unalloc_region:
        unregister_chrdev_region(mmap_dev, 1);
  out_vfree:
	dma_free_coherent (NULL, (NPAGES + 2) * PAGE_SIZE, alloc_ptr,
	    dma_handle);
  out:
        return ret;
}

/* module unload */
static void __exit mmap_alloc_exit(void)
{
        int i;

        /* remove the character deivce */
        cdev_del(&mmap_cdev);
        unregister_chrdev_region(mmap_dev, 1);

	/* free the memory areas */
	dma_free_coherent (NULL, (NPAGES + 2) * PAGE_SIZE, alloc_ptr,
	    dma_handle);
}

module_init(mmap_alloc_init);
module_exit(mmap_alloc_exit);
MODULE_DESCRIPTION("mmap_alloc driver");
MODULE_AUTHOR("Claudio Scordino and Bruno Morelli");
MODULE_LICENSE("GPL");

