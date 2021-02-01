Run an initial build

Move to test directory
  $ cd $TESTDIR

Prepare for a clean run
  $ rm -rf .dodo a b
  $ echo "hello" > a_input
  $ echo "world" > b_input
  $ chmod 0644 b_input

Compile a and b
  $ clang -o a a.c
  $ clang -o b b.c

Run the first build
  $ $DODO --show
  dodo-launch
  Dodofile
  ./a
  ./b

Run a rebuild
  $ $DODO --show

Clean up
  $ rm -rf .dodo a b
  $ chmod 0644 b_input