# Script generated partially by ChatGPT
# Requirement: pip install termcolor
import subprocess
import difflib
import os
import re
from termcolor import colored
import filecmp
import shutil

# Paths to your shell and the folder containing test files
SHELL_PATH = "../icshell"
TESTS_PATH = "./tests"
FILES_PATH = "./files"

def run_shell_command(command, shell_path, stdout_file, stderr_file):
    """
    Run a command using the specified shell and redirect stdout/stderr to files.
    """
    process = subprocess.Popen(
        [shell_path, '-c', command],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    try:
        stdout, stderr = process.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()

    with open(stdout_file, 'w') as f_out:
        f_out.write(stdout)

    with open(stderr_file, 'w') as f_err:
        f_err.write(stderr)

def run_bash_commands_independently(test_file, stdout_file, stderr_file):
    """
    Run each line of a test file as a separate bash command.
    """
    with open(test_file, 'r') as f:
        lines = f.readlines()

    stdout_lines = []
    stderr_lines = []

    for line in lines:
        line = line.strip()
        if not line:
            continue
        process = subprocess.Popen(
            ['bash', '-c', line],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        try:
            stdout, stderr = process.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()

        stdout_lines.append(stdout)
        stderr_lines.append(stderr)

    with open(stdout_file, 'w') as f_out:
        f_out.writelines(stdout_lines)

    with open(stderr_file, 'w') as f_err:
        f_err.writelines(stderr_lines)

def clean_ic_errors(stderr_file):
    """
    Remove "ICshell: " prefix from lines in the ICshell stderr file.
    """
    with open(stderr_file, 'r') as f_err:
        lines = f_err.readlines()

    cleaned_lines = []
    for line in lines:
        if line.startswith("ICshell: "):
            cleaned_lines.append(line[len("ICshell: "):])
        else:
            cleaned_lines.append(line)

    with open(stderr_file, 'w') as f_err:
        f_err.writelines(cleaned_lines)

def clean_bash_errors(stderr_file, test_file):
    """
    Remove "bash: line XX: " prefix from lines in the bash stderr file.
    Also remove "bash: -c: line XX:" and any `...' directly after
    """
    with open(stderr_file, 'r') as f_err:
        lines = f_err.readlines()

    test_name = os.path.basename(test_file)
    pattern = re.compile(rf"^bash: (-c: )?line \d+: (`(.*?)'$)?")

    cleaned_lines = []
    for line in lines:
        cleaned_line = re.sub(pattern, '', line)
        if cleaned_line != '\n':
            cleaned_lines.append(cleaned_line)

    with open(stderr_file, 'w') as f_err:
        f_err.writelines(cleaned_lines)

def compare_outputs(shell_file, bash_file):
    """
    Compare the outputs of your shell and bash.
    """
    with open(shell_file, 'r') as f_shell, open(bash_file, 'r') as f_bash:
        shell_output = f_shell.readlines()
        bash_output = f_bash.readlines()

    diff = list(difflib.unified_diff(shell_output, bash_output, fromfile='ICshell', tofile='bash'))

    if not diff:
        return "OK"
    else:
        print(f"\nDifferences found in output file {shell_file.rsplit('.', 1)[-1]}:")
        for line in diff:
            if line.startswith('+'):
                print(colored(line.strip(), 'green'))
            elif line.startswith('-'):
                print(colored(line.strip(), 'red'))
            else:
                print(line.strip())
        return "ERROR"

def compare_directories(dir1, dir2):
    """
    Compare two directories recursively.
    """
    comparison = filecmp.dircmp(dir1, dir2)
    if comparison.diff_files or comparison.left_only or comparison.right_only:
        print("Differences found in directory files:")
        if comparison.diff_files:
            for file in comparison.diff_files:
                print("Files differ:", file)
                with open(os.path.join(dir1, file), 'r') as f1, open(os.path.join(dir2, file), 'r') as f2:
                    diff = list(difflib.unified_diff(f1.readlines(), f2.readlines(), fromfile=dir1, tofile=dir2))
                    for line in diff:
                        if line.startswith('+'):
                            print(colored(line.strip(), 'green'))
                        elif line.startswith('-'):
                            print(colored(line.strip(), 'red'))
                        else:
                            print(line.strip())
        if comparison.left_only:
            print("Files only in", dir1, ":", comparison.left_only)
        if comparison.right_only:
            print("Files only in", dir2, ":", comparison.right_only)
        return "ERROR"
    return "OK"

def run_test(test_file):
    test_name = os.path.splitext(test_file)[0]
    output_dir = os.path.join(os.getcwd(), test_name)

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    shell_stdout_file = os.path.join(output_dir, f"{test_name}.shell.stdout")
    shell_stderr_file = os.path.join(output_dir, f"{test_name}.shell.stderr")
    bash_stdout_file = os.path.join(output_dir, f"{test_name}.bash.stdout")
    bash_stderr_file = os.path.join(output_dir, f"{test_name}.bash.stderr")

    print(f"Running test: {test_file}")

    # Backup the original files directory
    files_backup_dir = os.path.join(os.getcwd(), 'files_backup')
    if os.path.exists(files_backup_dir):
        shutil.rmtree(files_backup_dir)
    shutil.copytree(FILES_PATH, files_backup_dir)

    os.system(f"chmod 000 {FILES_PATH}/denied")

    # Run test with your shell
    run_shell_command(os.path.join(TESTS_PATH, test_file), SHELL_PATH, shell_stdout_file, shell_stderr_file)
    clean_ic_errors(shell_stderr_file)

    os.system(f"chmod 644 {FILES_PATH}/denied")

    # Compare directory files after running with your shell
    shell_files_dir = os.path.join(output_dir, 'shell_files')
    shutil.copytree(FILES_PATH, shell_files_dir)

    # Restore the original files directory
    shutil.rmtree(FILES_PATH)
    shutil.copytree(files_backup_dir, FILES_PATH)

    os.system(f"chmod 000 {FILES_PATH}/denied")

    # Run test with bash, each line independently
    run_bash_commands_independently(os.path.join(TESTS_PATH, test_file), bash_stdout_file, bash_stderr_file)
    clean_bash_errors(bash_stderr_file, test_file)

    os.system(f"chmod 644 {FILES_PATH}/denied")

    # Compare directory files after running with bash
    bash_files_dir = os.path.join(output_dir, 'bash_files')
    shutil.copytree(FILES_PATH, bash_files_dir)

    # Compare stdout
    result_stdout = compare_outputs(shell_stdout_file, bash_stdout_file)

    # Compare stderr
    result_stderr = compare_outputs(shell_stderr_file, bash_stderr_file)

    # Compare directory files
    result_files = compare_directories(shell_files_dir, bash_files_dir)

    overall_result = "OK" if result_stdout == "OK" and result_stderr == "OK" and result_files == "OK" else "ERROR"
    print(f"Result: {overall_result}")
    print("=" * 50)

def run_tests():
    test_files = os.listdir(TESTS_PATH)
    for test_file in test_files:
        try:
            run_test(test_file)
        except Exception as e:
            print(f"Error running test {test_file}: {e}")
            continue  # Continue with the next test file

if __name__ == "__main__":
    run_tests()
