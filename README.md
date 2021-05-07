# ZippySolver 

An open source solver developed by the Zenith Poker community.

# Installation

The instructions here are for Windows 10.

3. Some modification to original files and build in CYGWIN.
    ```

5.  Install ZippySolver through git:

    ```bash
    git clone https://github.com/ZenithPoker/ZippySolver
    cd ZippySolver/ZippyEngine
    ```
	
6.	Build whatever binaries you need from the top level. For example:

	```
	make bin/build_hand_value_tree
	```

7.	TODO


Test: 
	```
cd /home/jinyi/ZippySolver/ZippyEngine/runs
1. build game tree
../bin/build_hand_value_tree.exe ms0_params
2. build betting tree
../bin/build_betting_tree ms0_params mb1b1_params
3. Run the test.
../bin/run_cfrp ms0_params none_params mb1b1_params cfrps_params 8 1 200
	```