puts ""
puts "----- test: breakpoint in 'Main::Main()' -----"
puts ""

run_genode_until {\(gdb\)} 60 $gdb_id

send "b Main::Main\n"
run_genode_until {\(gdb\)} 20 $gdb_id
send "c\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {Thread 1.2 "ep" hit Breakpoint 2, Main::Main} $output]} {
	puts stderr "*** Error: Breakpoint in Main::Main() did not trigger"
	exit -1
}

send "delete 2\n"
run_genode_until {\(gdb\)} 20 $gdb_id

puts "\n"
puts "----- test: breakpoint in shared library -----"
puts ""

send "b Genode::cache_coherent(unsigned long, unsigned long)\n"
run_genode_until {\(gdb\)} 20 $gdb_id
send "c\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {Breakpoint 3, Genode::cache_coherent ()} $output]} {
	puts "*** Error: Breakpoint in shared library did not trigger"
	exit -1
}

puts "\n"
puts "----- test: stack trace when not in syscall -----"
puts ""

send "bt\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {#0  Genode::cache_coherent ()} $output] ||
    ![regexp {in func2 ()} $output] ||
    ![regexp {in func1 ()} $output] ||
    ![regexp {in Main::Main} $output]} {

	puts stderr "*** Error: Stack trace when not in syscall is not as expected"
	exit -1
}

puts "\n"
puts "----- test: modification of a variable value -----"
puts ""

send "print test_var\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {\$1 = 1} $output]} {
	puts stderr "*** Error: first 'print test_var' command didn't result in the expected output"
	exit -1
}

send "set var test_var=2\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "print test_var\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {\$2 = 2} $output]} {
	puts stderr "*** Error: second 'print test_var' command didn't result in the expected output"
	exit -1
}

puts "\n"
puts "----- test: 'call' command -----"
puts ""

send "call test_var_func()\n"
run_genode_until {\(gdb\)} 60 $gdb_id

if {![regexp {\$3 = 3} $output]} {
	puts stderr "*** Error: 'call' command didn't result in the expected output"
	exit -1
}

puts "\n"
puts "----- test: thread info -----"
puts ""

send "b Test_thread::entry\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "c\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {Breakpoint 4, Test_thread::entry} $output]} {
	puts stderr "*** Error: Breakpoint in test thread did not trigger"
	exit -1
}

send "thread 4\n"
run_genode_until {\(gdb\)} 20 $gdb_id
if {[regexp {Unknown thread 1.4} $output]} {
	# probably on a platform without signal handler thread
	send "thread 3\n"
	run_genode_until {\(gdb\)} 20 $gdb_id
}

send "info threads\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {  1.1  Thread 1.1 "test-monitor_gdb" \(running\)} $output] ||
    ![regexp {  1.2  Thread 1.2 "ep"               \(running\)} $output] ||
    ![regexp {"thread"           Test_thread::entry} $output] ||
    ![regexp {  2.1  Thread 2.1 "test-log"         \(running\)} $output] ||
    ![regexp {  2.2  Thread 2.2 "ep"               \(running\)} $output]} {
	puts stderr "*** Error: Thread info is not as expected"
	exit -1
}

puts "\n"
puts "----- test: step into function -----"
puts ""

send "step\n"
run_genode_until {\(gdb\)} 30 $gdb_id

if {![regexp {Test_thread::test_step} $output]} {
	puts stderr "*** Error: Step into function didn't result in the expected output"
	exit -1
}

puts "\n"
puts "----- test: catching a segmentation fault -----"
puts ""

send "c\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {"thread" received signal SIGSEGV, Segmentation fault.} $output]} {
	puts stderr "*** Error: Segmentation fault exception was not caught"
	exit -1
}

puts "\n"
puts "----- test: stack trace when in syscall -----"
puts ""

send "thread 2\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "interrupt &\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "bt\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {Genode::Lock::lock} $output] ||
	![regexp {Main::Main} $output] } {

	puts stderr "*** Error: Stack trace when in syscall is not as expected"
	exit -1
}

puts "\n"
puts "----- test: memory map -----"
puts ""

send "info mem\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {..*y.*0x0000000000004000 0x0000000000006000 rw nocache} $output] ||
    ![regexp {..*y.*0x0000000000008000 0x0000000000010000 rw nocache} $output] ||
    ![regexp {..*y.*0x0000000000010000 0x0000000000012000 rw nocache} $output] ||
    ![regexp {..*y.*0x0000000000030000 0x00000000001..000 rw nocache} $output] ||
    ![regexp {7.*y.*0x0000000001000000 0x000000000100.000 rw nocache} $output] ||
    ![regexp {8.*y.*0x000000000100.000 0x000000000100.000 rw nocache} $output] ||
    ![regexp {9.*y.*0x00000000400fb000 0x00000000400ff000 rw nocache} $output] ||
    ![regexp {1..*y.*0x00000000401ef000 0x00000000401ff000 rw nocache} $output] ||
    ![regexp {1..*y.*0x00000000402f.000 0x00000000402ff000 rw nocache} $output]} {
	puts stderr "*** Error: memory map is not as expected"
	exit -1
}

puts "\n"
puts "----- test: stack trace of second inferior -----"
puts ""

send "inferior 2\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "thread 1\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "interrupt\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "file debug/test-log\n"
run_genode_until {\(gdb\)} 20 $gdb_id

send "bt\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {Genode::Signal_receiver::block_for_signal} $output] } {
	puts stderr "*** Error: Stack trace of second inferior is not as expected"
	exit -1
}

puts "\n"
puts "----- test: memory map of second inferior -----"
puts ""

send "info mem\n"
run_genode_until {\(gdb\)} 20 $gdb_id

if {![regexp {..*y.*0x0000000000008000 0x0000000000010000 rw nocache} $output] ||
    ![regexp {..*y.*0x0000000000010000 0x0000000000012000 rw nocache} $output] ||
    ![regexp {..*y.*0x0000000000030000 0x00000000001..000 ro nocache} $output] ||
    ![regexp {5.*y.*0x0000000001000000 0x000000000100.000 ro nocache} $output] ||
    ![regexp {6.*y.*0x000000000100.000 0x000000000100.000 rw nocache} $output] ||
    ![regexp {7.*y.*0x00000000400fb000 0x00000000400ff000 rw nocache} $output] ||
    ![regexp {..*y.*0x00000000401ef000 0x00000000401ff000 rw nocache} $output]} {
	puts stderr "*** Error: memory map of second inferior is not as expected"
	exit -1
}

puts ""
