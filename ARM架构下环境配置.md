#### Openeuler下安装HPCG

1. 下载源码

   ```bash
   git clone https://github.com/hpcg-benchmark/hpcg.git
   ```

2. 编译

   ```bash
   # 安装mpi
   sudo yum install mpich mpich-devel
   # 查看安装是否成功
   mpicc --version
   mpirun --version
   # find / -name mpicc 查找MPI安装路径
   # echo 'export PATH=/usr/lib64/mpich/bin:$PATH' >> ~/.bashrc
   # source ~/.bashrc
   
   # 创建 build 目录
   cd hpcg
   mkdir build
   cd build
   ../configure Linux_MPI
   make
   ```

3. 测试

   ```bash
   cd build/bin
   # 设置线程数为 8
   export OMP_NUM_THREADS=8
   mpirun -np 16 -- xhpcg
   # 绑定numa节点
   mpirun -np 16 numactl -N 0 -m 0 ./xhpcg
   ```


#### Lmbench安装和执行

1. 获取lmbench3源码

   ```bash
   wget https://jaist.dl.sourceforge.net/project/lmbench/development/lmbench-3.0-a9/lmbench-3.0-a9.tgz
   tar -zxvf lmbench-3.0-a9.tgz
   ```

2. 解压编译

   ```bash
   # 修改 scripts/build脚本，在LDLIBS=-lm下面添加两行
   LDLIBS="${LDLIBS} -ltirpc"
   CFLAGS="${CFLAGS} -l/usr/include/tirpc"
   
   sudo yum install libtirpc-devel
   ```

3. 执行测试

   ```bash
   cd lmbench-3.0-a9
   make results
   ```

4. 设置

   ```bash
   MULTIPLE COPIES: 同时运行并行测试，对应生成结果中的 scal load项；
   Job placement selection: 作业调度控制方法，默认选1表示允许作业调度；
   Options to control job placement: 默认选1；
   Memory: 设置为大于4倍的cache size，该值越大结果越精确，同时运行时间越长；
   SUBSET: 要运行的子集，包含 ALL/HARWARE/OS/DEVELOPMENT，默认选ALL；
   其他选项默认即可
   Mail results: 发送邮件默认是yes，输入no。
   设置完成后测试程序开始运行，需要等待
   ```

5. Lmbench结果分析

   ```bash
   cd lmbench-3.0-a9
   make see
   # 若只出现两行命令，显示运行结果输出到了summary.out文件中，直接查看该文件即可：
   cat ./results/summary.out
   ```

6. 单独测试

   ```bash
   # bin目录下有单独测试的程序
   cd bin
   # 带宽测试
   # bw_mem [-P 并发数] [-W 预热次数] [-N 重复次数] <size 大于512B> what
   # what包括rd、wr、rdwr、cp、fwr、frd、fcp、bzero、bcopy
   ./bw_mem -P 38 -W 5 -N 5 64M frd
   # 延迟测试
   # lat_mem_rd [-P 并发数] [-W 预热次数] [-N 重复次数] [-t 指定表示随机访问] len [stride 步长]
   ./lat_mem_rd -P 38 -W 5 -N 5 -t len
   ```
   

#### Openeuler下安装HPL

其他步骤和x86上一样

```bash
# 编译时出现make[2]: Entering directory '/home/zmq/hpl/src/comm/OpenEuler'
/usr/include/openmpi-aarch64/mpi.h:327:59: error: expected expression before ‘_Static_assert’
按照报错修改/hpl/src/comm/HPL_packL.c中对应的函数名
MPI_Address 改成 MPI_Get_address
MPI_Type_struct 改成 MPI_Type_create_struct
# 安装 blas 库
yum 直接安装或者手动安装libblas
wget http://www.netlib.org/blas/blas.tgz
tar -xvzf blas.tgz
cd BLAS
make
sudo cp libblas.a /usr/local/lib/
sudo cp libblas.so /usr/local/lib/
sudo cp *.h /usr/local/include/
sudo ldconfig
```

```bash
第4行 6   device out (6=stdout,7=stderr,file)  
device out"为"6"时，测试结果输出至标准输出（stdout）
"device out"为"7"时，测试结果输出至标准错误输出（stderr）
"device out"为其它值时，测试结果输出至第三行所指定的文件中
```



#### Openeuler下安装WRF

[Compiling WRF](https://www2.mmm.ucar.edu/wrf/OnLineTutorial/compilation_tutorial.php)

[Full WRF and WPS Installation Example (GNU) | WRF & MPAS-A Support Forum](https://forum.mmm.ucar.edu/threads/full-wrf-and-wps-installation-example-gnu.12385/)

其他和x86一样

```bash
使用gcc 9.4.0版本
export PATH=/opt/gcc-9.4.0/bin/:$PATH
export FC=/opt/gcc-9.4.0/bin/gfortran

DIR=/home/zmq/Build_WRF/LIBRARIES
export NETCDF=$DIR/netcdf
export LD_LIBRARY_PATH=$NETCDF/lib:$DIR/grib2/lib
export PATH=$NETCDF/bin:$DIR/mpich/bin:${PATH}
export JASPERLIB=$DIR/grib2/lib
export JASPERINC=$DIR/grib2/include

export CC=gcc
export CXX=g++
#export FC=gfortran
#export FCFLAGS="-m64 -fallow-argument-mismatch"
export FCFLAGS="-march-armv8-a -Wno-argument-mismatch"
export F77=gfortran
#export FFLAGS="-m64 -fallow-argument-mismatch"
export FFLAGS="-march-armv8-a -Wno-argument-mismatch"
export LDFLAGS="-L$NETCDF/lib -L$DIR/grib2/lib"
export CPPFLAGS="-I$NETCDF/include -I$DIR/grib2/include -fcommon"
export FFLAGS='-Wno-argument-mismatch'
！！环境变量
gcc -c -m64 TEST_4_fortran+c_c.c
在arm架构下-m64改成 -march=armv8-a
安装libpng时 ./configure出现无法识别编译的操作系统
./configure --prefix=$DIR/grib2 --build=arm-linux

编译WRF时，修改 arch/configure.defaults
第二段内容 47行开始改成
###########################################################
#ARCH    Linux aarch64, gfortran compiler with gcc #serial smpar dmpar dm+sm
#
DESCRIPTION     =       GNU ($SFC/$SCC)
DMPARALLEL      =       # 1
OMPCPP          =       # -D_OPENMP

OMP             =       # -fopenmp
OMPCC           =       # -fopenmp
SFC             =       gfortran
SCC             =       gcc
CCOMP           =       gcc
DM_FC           =       mpif90 -f90=$(SFC)
DM_CC           =       mpicc -cc=$(SCC)
FC              =       gfortran
CC              =       gcc
LD              =       $(FC)
RWORDSIZE       =       CONFIGURE_RWORDSIZE
PROMOTION       =       #-fdefault-real-8
ARCH_LOCAL      =       -DNONSTANDARD_SYSTEM_SUBR  -DWRF_USE_CLM
CFLAGS_LOCAL    =       -w -O3 -c -march=armv8-a -mcpu=cortex-a53 # -DRSL0_ONLY
LDFLAGS_LOCAL   =		-L/usr/local/lib
CPLUSPLUSLIB    =
ESMF_LDFLAG     =       $(CPLUSPLUSLIB)
FCOPTIM         =       -O2 -ftree-vectorize -funroll-loops -mcpu=cortex-a53 -march=armv8-a
FCREDUCEDOPT    =       $(FCOPTIM)
FCNOOPT         =       -O0
FCDEBUG         =       # -g $(FCNOOPT)  # -fbacktrace -ggdb -fcheck=bounds,do,mem,pointer -ffpe-trap=invalid,zero,overflow
FORMAT_FIXED    =       -ffixed-form
FORMAT_FREE     =       -ffree-form -ffree-line-length-none
FCSUFFIX        =
BYTESWAPIO      =       -fconvert=big-endian -frecord-marker=4
FCBASEOPTS_NO_G =       -w $(FORMAT_FREE) $(BYTESWAPIO)
FCBASEOPTS      =       $(FCBASEOPTS_NO_G) $(FCDEBUG)
MODULE_SRCH_FLAG =
TRADFLAG        =      CONFIGURE_TRADFLAG
CPP             =      /lib/cpp CONFIGURE_CPPFLAGS
AR              =      ar
ARFLAGS         =      ru

./configure 选1
./compile -j 8 em_grav2d_x 2>&1 | tee -a compile.log
```



#### 设置内核默认启动项

```bash
make openeuler_defconfig #设置默认的config
vim .config 
CONFIG_SPE_MIGRATING=y	#设置页面分析迁移配置
```

在/boot/efi/EFI/openeuler/grub.cfg中找到对应内核的cmdline，加上mem_sampling_on

![image-20250110155245581](C:\Users\10782\AppData\Roaming\Typora\typora-user-images\image-20250110155245581.png)

1. 查看当前的 GRUB 启动项

   ```bash
   grep menuentry /boot/grub2/grub.cfg
   # 可以看到类似这样的输出项
   menuentry 'openEuler (5.10.0-235.0.0.134.oe2203sp4.aarch64) 22.03 (LTS-SP4)' ...
   menuentry 'openEuler (5.10.0-226.0.0.125.oe2203sp4.aarch64) 22.03 (LTS-SP4)' ...
   menuentry 'openEuler (5.10.0-225.0.0.124.oe2203sp4.aarch64) 22.03 (LTS-SP4)' ...
   menuentry 'openEuler (5.10.0) 22.03 (LTS-SP4)' ...
   ```

2. 设置默认启动项

   ```bash
   sudo grub2-set-default 'openEuler (5.10.0) 22.03 (LTS-SP4)'
   ```

3. 更改grub配置

   ```bash
   # 执行后会根据/boot/grub2/grub.cfg中的启动命令行参数重新生成/boot/efi/EFI/openeuler/grub.cfg
   sudo grub2-mkconfig -o /boot/grub2/grub.cfg
   # 
   sudo reboot
   uname -r
   ```
   
   ```bash
   # 查看默认启动内核
   grub2-editenv list
   # 启用spe采样
   echo 1 > /proc/sys/kernel/mem_sampling_enable
   echo 1 > /proc/sys/kernel/spe_migrating_mem_sampling
   # echo enabled > /sys/fs/cgroup/memory/spe_migrating_enabled
   # 查看mem_sampling
   dmesg | grep mem_sampling
   # 查看spe日志
   dmesg | grep spe
   # 查看中断数
   cat /proc/interrupts | grep spe
   ```
   
   

```bash
# 查看当前cgroup版本
mount|grep cgroup

[root@localhost htmm]# mount|grep cgroup
cgroup2 on /sys/fs/cgroup type cgroup2 (rw,nosuid,nodev,noexec,relatime,seclabel,nsdelegate,memory_recursiveprot)

#修改内核启动cmdline，改用cgroup v2版本
删除cmdline中的 cgroup_disable=files
增加 systemd.unified_cgroup_hierarchy=1
```

在 Linux 系统中，`/sys/fs/cgroup` 目录用于挂载 `cgroup` 文件系统，具体是挂载 `cgroup v1` 还是 `cgroup v2`。你可以查看该目录的结构来确认使用的是哪个版本。

- **cgroup v2**：如果你的系统使用 `cgroup v2`，你会看到整个控制组在 `/sys/fs/cgroup` 目录下是一个单一的目录，而不是多个子目录。
- **cgroup v1**：如果是 `cgroup v1`，则会看到多个按资源类别（如 `cpu`, `memory` 等）划分的子目录。



![image-20250304191305408](C:\Users\10782\AppData\Roaming\Typora\typora-user-images\image-20250304191305408.png)

```bash
# 先绑定 shell
echo $$ > /sys/fs/cgroup/memory/my_cgroup/cgroup.procs
# 再运行进程
./my_program
# 启用spe采样分析和迁移，在当前窗口下执行的进程都将属于htmm
./set_htmm_memcg.sh htmm $$ enable
# 查看当前窗口运行的进程pid
$$
# 当当前窗口所有的进程都运行结束后，关闭htmm，否则将触发空指针bug
./set_htmm_memcg.sh htmm $$ disable

# 关闭NUMA Balancing
echo 0 > /proc/sys/kernel/numa_balancing
```






```c
#define _GNU_SOURCE // 必须添加，启用 CPU 亲和性相关函数
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h> // 用于 CPU 亲和性相关操作

#define THREAD_NUM 4

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int active_threads = THREAD_NUM;
int waiting_threads = 0;

void set_thread_affinity(int thread_id) {
    cpu_set_t cpuset;           // 定义 CPU 集合
    CPU_ZERO(&cpuset);          // 初始化集合
    CPU_SET(thread_id % 39, &cpuset); // 将线程绑定到 CPU 0-38 的某一个核心上

    // 设置线程亲和性
    pthread_t current_thread = pthread_self(); // 获取当前线程句柄
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np failed");
        exit(EXIT_FAILURE);
    }
    printf("Thread %d: Bound to CPU %d\n", thread_id, thread_id % 39);
}

void *thread_func(void *arg) {
    int thread_id = *(int *)arg;

    // 设置线程 CPU 亲和性
    set_thread_affinity(thread_id);

    printf("Thread %d: Started.\n", thread_id);

    if (thread_id == 2) { // Simulate an exception for thread 2
        printf("Thread %d: Simulating failure.\n", thread_id);

        pthread_mutex_lock(&mutex);
        active_threads--;
        pthread_mutex_unlock(&mutex);

        pthread_exit(NULL);
    }

    // Perform some work
    printf("Thread %d: Performing work.\n", thread_id);

    // Barrier logic
    pthread_mutex_lock(&mutex);
    waiting_threads++;
    if (waiting_threads == active_threads) {
        // Last thread to arrive at the barrier
        // waiting_threads = 0; // Reset for next use
        pthread_cond_broadcast(&cond);
    } else {
        while (waiting_threads < active_threads) {
            pthread_cond_wait(&cond, &mutex);
        }
    }
    pthread_mutex_unlock(&mutex);

    printf("Thread %d: Passed the barrier.\n", thread_id);

    pthread_exit(NULL);
}

int main() {
    pthread_t threads[THREAD_NUM];
    int thread_ids[THREAD_NUM];

    // Create threads
    for (int i = 0; i < THREAD_NUM; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_func, &thread_ids[i]) != 0) {
            perror("pthread_create failed");
            return EXIT_FAILURE;
        }
    }

    // Join threads
    for (int i = 0; i < THREAD_NUM; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All threads completed.\n");
    return 0;
}
```

