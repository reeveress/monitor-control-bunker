import keepAliveClass 
import argparse 

parser = argparse.ArgumentParser(description = 'This script continuously pokes an Arduino with the specified IP address. It also checks for command flag change inside the Redis database that are set through the nodeControlClass.',
                                    formatter_class = argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('ip_addr', action = 'store', help = 'Specify the IP address of the Arduino to keep alive (i.e. x.x.x.x)')
parser.add_argument('node', action = 'store', help = 'Specify the node number')
args = parser.parse_args()



h = keepAliveClass.KeepAlive(args.ip_addr, int(args.node))
h.keepAlive() 

