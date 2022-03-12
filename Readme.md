# Finix Line Protocol (FLP)

A simple interfacing protocol to interact with the Finix System.

# Spec

All system state shall be reported on reset and on update. But there is no way to request the value on-the-fly. If the
states are out of synchronization, the system must be reset to ensure the synchronization.

There is no explicit setter of states/parameters. They are applied when a command is invoked by passing as the
arguments. All arguments are presumed persistent and won't restore once the command is finished. The following command
will overwrite the arguments. If the command fails, the parameter WILL be restored in sake of transaction.

`node.subnode.command [arg=value] [arg=value]`


When a command is invoked, there will be an immediate response sending with `_` label, and the same tag as the command.
If successful, the message will be OK. Errors will be reported in both immediate response and the error channel.

The host should not expect further response from this command. All async operations that may change the states will be 
reported via the 'R' label with the tag set to the state name. The host should monitor these tag instead.
