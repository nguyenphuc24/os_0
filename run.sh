./os os_0_mlq_paging  > input/os_0_mlq_paging.output
./os os_1_mlq_paging > input/os_1_mlq_paging.output
./os os_1_mlq_paging_small_1K > input/os_1_mlq_paging_small_1K.output
./os os_1_mlq_paging_small_4K > input/os_1_mlq_paging_small_4K.output
./os os_1_singleCPU_mlq > input/os_1_singleCPU_mlq.output
./os os_1_singleCPU_mlq_paging > input/os_1_singleCPU_mlq_paging.output
./os sched > input/sched.output
./os sched_0 input/sched_0.output
./os sched_1 > input/sched_1.output
./os os_sc > input/os_sc.output
./os os_syscall > input/os_syscall.output
./os os_syscall_list > input/os_syscall_list.output
mv input/*.output test/
