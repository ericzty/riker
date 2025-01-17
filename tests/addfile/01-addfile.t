Run an initial build

Move to test directory
  $ cd $TESTDIR

Prepare for a clean run
  $ rm -rf .rkr program helper2.c
  $ cp files/main.c.original main.c

Run the first build
  $ rkr --show-full
  rkr-launch
  Rikerfile
  **clang -o program helper1.c main.c (glob)
  **clang -o /tmp/* -c helper1.c (glob)
  **clang -o /tmp/* -c main.c (glob)
  **clang -o program * * (glob)
  **ld * (glob)

Check the output
  $ ./program
  helper 1

Move in the new file and modified main.c
  $ cp files/helper2.c .
  $ cp files/main.c.modified main.c

Update the build
  $ rkr --show-full
  Rikerfile
  **clang -o program helper1.c helper2.c main.c (glob)
  **clang -o /tmp/* -c helper2.c (glob)
  **clang -o /tmp/* -c main.c (glob)
  **clang -o program /tmp/* /tmp/* /tmp/* (glob)
  **ld * (glob)

Check the output
  $ ./program
  helper 1
  helper 2

Clean up
  $ rm -rf .rkr program helper2.c
  $ cp files/main.c.original main.c
