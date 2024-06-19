##
# hermes-broadcast
#
# @file
# @version 0.1

broadcast: broadcast.c kiss.c kiss.h
	gcc -o broadcast broadcast.c kiss.c

# end
