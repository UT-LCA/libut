# Threading Benchmarks

For the libut version of tbench, first build libbase.a, libruntime.a,
librt++.a and hwallocd, then launch hwallocd in the backgroud. The
following commands build and run the libut version of tbench:
~~~
$ make clean && make
$ ./tbench tbench.config
~~~

For the qthread version of tbench, first get qthread library ready
(compiled or installed), then the following commands build and run the
qthread version of tbench:
~~~
$ make tbench_qthread QTHREAD_INC=<path to qthread headers> QTHREAD_LIB=<path to qthread library>
$ ./tbench_qthread
~~~

For the go version of tbench, enter the subfolder `tbench_go`, then
the run the benchmark with:
~~~
$ go run .
~~~
