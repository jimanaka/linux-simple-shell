This program was compiled and tested on a x86_64 GNU/Linux (kernel 6.1.8-arch) machine.

The shell supports all requirements including piping, multi piping, IO redirection, and backgrounding.

NOTE: commands such as "cmd1 > wc | cmd2" and "cmd1 | cmd2 < file" are not supported due to ambiguous IO.

One partially non-standard function was used -- execvpe(). However this function is part of the GNU c standard and it should work on any GNU/Linux system. This function extends the functionality of execvp() by allowing for the specification of a PATH to look for executables.


