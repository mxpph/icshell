# icshell
This is a custom shell written as an extension to the Imperial College Computing first-year C group project.
The main part of the project is to create an ARM emulator and assembler, but as the project is repeated every year, we will not publish the code that would be relevant to any future first-year students, in order to prevent plagiarism. If you would like to see this code, please contact me privately.

**The authors are Xavier Gilli, Adrian Leung, Conan Li, and Xuefeng Xu.**

---

### icshell supports: 
- command execution with arguments from relative and absolute paths as well as from the `PATH` variable.
- file redirections, including here-documents.
- pipes
- setting and expansion of environment variables, including the exit status `$?` and some other special variables.
- basic signal handling (SIGINT and SIGQUIT)
