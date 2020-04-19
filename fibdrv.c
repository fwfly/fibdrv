#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mutex.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 500

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);


static ktime_t kt;

struct BigN {
    unsigned long long lower, upper;
};

static inline void addBigN(struct BigN *out, struct BigN x, struct BigN y)
{
    out->upper = x.upper + y.upper;
    unsigned long long lower = ~x.lower;
    if (y.lower > lower) {
        out->upper++;
    }
    out->lower = x.lower + y.lower;
}


/* Here is an assumption is x is always bigger then y */
static inline void subBigN(struct BigN *out, struct BigN x, struct BigN y)
{
    out->upper = x.upper - y.upper;
    if (y.lower > x.lower) {
        x.upper--;
        out->lower = ULONG_MAX - y.lower + x.lower;
    } else {
        out->lower = x.lower - y.lower;
    }
    // printk("%lld = %lld - %lld", out->lower, x.lower, y.lower);
}

/* Multi is loop addBigN y times */
/*static inline void multiBigN(struct BigN *out, struct BigN x, struct BigN y)
{

    out->upper = 0;
    out->lower = 0;
    // unsigned long long tmpx = x.lower, tmpy = y.lower;

    if (!y.upper && y.lower >= 1) {
        out->upper = x.upper;
        out->lower = x.lower;
    }

    while ((y.upper != 0) || (y.lower > 1)) {
        while (y.lower > 1) {
            addBigN(out, *out, x);
            y.lower--;
        }
        if (y.upper) {
            y.upper--;
            y.lower = ULONG_MAX;
        }
    }
    // printk("Mult %lld = %lld * %lld", out->lower, tmpx, tmpy);
}*/


static inline void multiBigN64to128(struct BigN *out,
                                    unsigned long long x,
                                    unsigned long long y)
{
    uint32_t a = x & 0xFFFFFFFF;
    uint32_t c = x >> 32;
    uint32_t b = y & 0xFFFFFFFF;
    uint32_t d = y >> 32;
    uint64_t ab = (uint64_t) a * b;
    uint64_t bc = (uint64_t) b * c;
    uint64_t ad = (uint64_t) a * d;
    uint64_t cd = (uint64_t) c * d;
    uint64_t low = ab + (bc << 32);
    uint64_t high = cd + (bc >> 32) + (ad >> 32) + (low < ab);
    low += (ad << 32);
    high += (low < (ad << 32));
    out->lower = low;
    out->upper = high;
}

static inline void multiBigN(struct BigN *out, struct BigN x, struct BigN y)
{
    out->upper = 0;
    out->lower = 0;
    unsigned long long h = x.lower * y.upper + x.upper * y.lower;
    multiBigN64to128(out, x.lower, y.lower);
    x.upper += h;
    out->upper = x.upper;
}

static inline void assignBigN(struct BigN *x, struct BigN y)
{
    x->upper = y.upper;
    x->lower = y.lower;
}

// To pass the clang-format, we comment this code.
// This function is useful when we need to verify the number to human-readable.
/*void BigN_to_int(struct BigN *res, struct BigN x)
{
    unsigned long long max10 = 10000000000000000000U;
    unsigned long long idx = x.upper;
    unsigned long long max_first = ULONG_MAX / max10;
    unsigned long long max_mod = ULONG_MAX - max_first * max10;

    res->lower = x.lower;
    unsigned long long x_first = x.lower / max10;
    unsigned long long x_mod = x.lower - x_first * max10;

    while (idx) {
        // Add mod
        x_mod = x_mod + max_mod;
        int carry = 0;
        // count if it needs carry over.
        if (x_mod > max10) {
            carry = 1;
            x_mod = x_mod - max10;
        }
        res->lower = x_mod;
        // Add x_first , max_first, carry to find upper_dec
        x_first = x_first + max_first + carry;
        res->upper = x_first;
        idx--;
    }
}*/

static int num_bits(long long k)
{
    int num = 0;
    while (k) {
        k = k / 2;
        num++;
    }
    return num;
}

static long long fast_fib_sequence(long long k)
{
    struct BigN a, b, b2, t1, t2, t2a;
    a.upper = 0;
    a.lower = 0;
    b.upper = 0;
    b.lower = 1;
    b2.upper = 0;
    b2.lower = 2;
    t1.upper = 0;
    t1.lower = 0;
    t2.upper = 0;
    t2.lower = 0;
    t2a.upper = 0;
    t2a.lower = 0;

    int numbits = num_bits(k);
    int count = numbits;

    while (count) {
        /* t1 = a*(2*b - a) */
        multiBigN(&t1, b, b2);
        subBigN(&t1, t1, a);
        multiBigN(&t1, t1, a);

        /* t2 = b^2 + a^2; */
        multiBigN(&t2, b, b);
        multiBigN(&t2a, a, a);
        addBigN(&t2, t2, t2a);

        /* a = t1; b = t2; // m *= 2 */
        assignBigN(&a, t1);
        assignBigN(&b, t2);
        // int tmpk = (k >> (count -1)) & 1;
        // printk("%d ",tmpk );
        if ((k >> (count - 1)) & 1) {
            // printk("%d : special count\n", tmpk);
            addBigN(&t1, a, b);  // t1 = a + b;
            assignBigN(&a, b);   // a = b;
            assignBigN(&b, t1);  // b = t1;
        }
        count--;
    }
    /* struct BigN res;
    res.upper = 0;
    res.lower = 0;
    BigN_to_int( &res, a );
    if (res.upper)
        printk("%lld: %lld %lld\n", k, res.upper, res.lower);
    else
        printk("%lld: %lld\n", k, res.lower); */
    return a.lower;
}


/*static long long fib_sequence(long long k)
{
    if (!k)
        return 0;

    struct BigN f[k + 2];

    f[0].upper = 0;
    f[0].lower = 0;
    f[1].upper = 0;
    f[1].lower = 1;

    for (int i = 2; i <= k; i++) {
        addBigN(&f[i], f[i - 1], f[i - 2]);
    }

    return f[k].lower;
}*/

static long long fib_ktime_proxy(long long k)
{
    kt = ktime_get();
    // long long result = fib_sequence(k);
    long long result = fast_fib_sequence(k);
    kt = ktime_sub(ktime_get(), kt);
    return result;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    return (ssize_t) fib_ktime_proxy(*offset);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return ktime_to_ns(kt);
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    printk("init fib-------\n");
    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
