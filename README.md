# sequence
  sequence - Run all executables in a directory in sequence.

## downloads

The sequence tool is available as RPMs through [COPR] as follows:

```
dnf copr enable minfrin/sequence
dnf install sequence
```

## synopsis
  sequence [-0] [-i] [-p] [-s facility.level] [-v] [-h] directory [options]

## description

  The sequence command runs all the executables in a specified directory,
  running each one in sequence ordered alphabetically.

  Each executable is named clearly in argv[0] or ${0}, and this
  name is prefixed to stderr or syslog to be clear which executable is
  responsible for output.

  Sequence is an alternative to the run-parts command found in cron.

## options
  -0, --zero  Terminate names with a zero instead of newline.

  -i, --ignore  Ignore non executable files. See the note below.

  -p, --print  Print the name of executables rather than execute.

  -s, --syslog [facility.]level  Send stderr to syslog at the given facility
                                 and level. Example: user.info

  -h, --help  Display this help message.

  -v, --version  Display the version number.

## return value
  The sequence tool returns the return code from the
  first executable to fail.

  If the executable was interrupted with a signal, the return
  code is the signal number plus 128.

  If the executable could not be executed, or if the options
  are invalid, the status 1 is returned.

## notes
  When non executable files are ignored with the -i option, sequence will
  ignore the EACCESS result code when trying to execute the file and move
  on to the next executable. When executables are listed with -p,
  sequence will make a 'best effort' check as to whether it is allowed
  to run an executable, ignoring any executables that are not regular files,
  and ignoring any executables that do not pass an access check. Callers are
  to take care using this information to ensure that race conditions and
  additional restrictions like selinux do not negatively affect the outcome.

## examples
  In this basic example, we execute all commands in /etc/rc3.d, passing
  the parameter 'start' to each command.
	~$ sequence /etc/rc3.d -- start

  Here, we execute all commands in /etc/cron.d, passing stderr to syslog
  with the level 'cron' and priority 'info'.
        ~$ sequence -s cron.info /etc/cron.d

## author
  Graham Leggett <minfrin@sharp.fm>

  [COPR]: <https://copr.fedorainfracloud.org/coprs/minfrin/sequence/>
