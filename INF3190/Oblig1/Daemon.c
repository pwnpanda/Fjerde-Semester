#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <bits/ioctls.h>

//To get Protocol.c, 
#include "Protocol.c"

//Defines the maximum connections to the select-server! Can be changed for testing purposes!
#define maxCon 1000

//Arp-register struct. Contains the MIP-address and the MAC-address of a daemon, and a pointer to the next ARP-entry.
struct Arp_list
{	
	char MIP[1];
	uint8_t MAC[6];
	struct Arp_list *next;
};

//Global variables, raw&ipc are sockets, retSet handles if ret is given a value, and the same for frmSet and tmpFrame.  
int raw, ipc, retSet=0, frmSet=0;
char ret[1];
struct MIP_Frame *tmpFrame;
char *daemonName;
//Len saves the lengt of the message, first is the pointer to the base node of the ARP-registry and myAdr is the MAC-address of this daemon.
size_t len=0;
struct Arp_list* first;
uint8_t myAdr[6];

//Clears the ARP-registry
void clearArp()
{
	struct Arp_list* this=first;
	struct Arp_list* prev;
	
	while(this->next != NULL){
		prev=this;
		this=this->next;
		free(prev);
	}
	free(this);
}
//Closes the program and frees all allocated variables.
void closeProg(){
	close(ipc);
	close(raw);
	unlink(daemonName);
	free(daemonName);
	if(frmSet){
		free(tmpFrame);
	}
	clearArp();

	printf("\nSystem closing!\n");
	exit(0);
}
//Saves the MAC-address of this daemon in the variable hwaddr, which is the third parameter. Returns 0 if failed and 1 if success.
static int get_if_hwaddr(int sock, const char* devname, uint8_t hwaddr[6])
{
	struct ifreq ifr;
	//clears the struct
	memset(&ifr, 0, sizeof(ifr));
	//Makes sure the struct has room for the content and copies content
	assert(strlen(devname) < sizeof(ifr.ifr_name));
	strcpy(ifr.ifr_name, devname);
	//Grabs address
	if(ioctl(sock, SIOCGIFHWADDR, &ifr) < 0)
	{
		perror("ioctl");
		return -4;
	}
	//Copies all the results and saves them.
	memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, 6*sizeof(uint8_t));

	return 0;
}
//Finds the corresponding MAC-address to the MIP-address. The first parameter is the MIP-address, and the second is where we store the MAC-address found.
//Returns 1 on success, returns 0 on failure.
int findArp(char dst[], uint8_t* macAd)
{	
	if(first->next!=NULL){
		struct Arp_list* this=first->next;
		//Loops through the ARP-registry for the correct MAC-address
		do{
			if(this->MIP[0] == (char)dst[0]){
				memcpy(macAd, this->MAC, 6);
				return 1;
			}
			this=this->next;
		}while(this->next != NULL);
	}
	return 0;
}
//Saves an APR-result in the ARP-registry. Parameters are the MIP-address and the MAC-address.
int saveArp(char dst[], uint8_t* mac)
{
	struct Arp_list* this=first;
	//Finds the last entry
	while(this->next != NULL)	this=this->next;
	//Creates a new entry and adds the information. Then adds the entry to the end of the list.
	struct Arp_list* add=malloc(sizeof(struct Arp_list));
	add->MIP[0]= (char) dst[0];
	memcpy(add->MAC, mac, 6);
	this->next=add;
	add->next=NULL;

	return 1;
}
//Prints the whole Arp-table.
void printArp()
{
	printf("\nArp_list:\n");
	if(first->next != NULL){
		struct Arp_list* this=first->next;
		while(this->next != NULL){
			printf("MIP-ADR: %c\n", this->MIP[0]);
			printf("MAC-ADR: ");
			printMAC(this->MAC);
			this=this->next;
		}

		printf("MIP-ADR: %c\n", this->MIP[0]);
		printf("MAC-ADR: ");
		printMAC(this->MAC);
		printf("---------------------\n\n");
	}
}
//Decodes the input from the client. The first parameter is the raw input and the other parameters are where the correct information is stored.
void decodeBuf(const char* buf, char* msg, char* dst)
{	
	int i;
	//Loops through input string
	for(i=0;i<strlen(buf);i++){
		//If it finds "__", then it saves the char after "__" as the destination MIP-address and breaks the for-loop.
		if(buf[i] == '_' && buf[i+1] == '_'){
			dst[0]=buf[i+2];
			break;
		}
		//Saves the message taken from the buffer.
		msg[i]=buf[i];
	}
	//Null-terminates the string.
	msg[i]='\0';
}
//Main TODO - Continue here! Make design-document and finish comments
int main(int argc, char* argv[]){

	if(argc != 3){
		printf("Usage: <Daemon-name> <Interface>\n");
		return -1;
	}


	const char* interface = argv[2];
	uint8_t iface_hwaddr[6];

	//Handles ctrl+c, for closing the server
	signal(SIGINT, closeProg);

	int err, accpt=0;
	fd_set fds;

	//Creates a socket for requests.
	raw=socket(AF_PACKET, SOCK_RAW, 0xFFFF);

	if(raw == -1){
		perror("Socket");
		close(raw);
		return -2;
	}

	//Makes sure that the socket always can be re-used, if recently used for another connection
	int activate=1;
	err = setsockopt(raw, SOL_SOCKET, SO_REUSEADDR, &activate, sizeof(int));
	//If above operation gives error.
	if(err==-1){
		perror("setsockopt");
		close(raw);
		return -3;
	}

	//
	if(get_if_hwaddr(raw, interface, iface_hwaddr) != 0)	return -3;

	memcpy(myAdr, iface_hwaddr,sizeof(iface_hwaddr));

	#ifdef DEBUG
	/* Print the hardware address of the interface */
	printf("HW-addr: ");
	printMAC(iface_hwaddr);
	#endif

	/* Bind the socket to the specified interface */
	struct sockaddr_ll device;
	memset(&device, 0, sizeof(device));

	device.sll_family = AF_PACKET;
	device.sll_ifindex = if_nametoindex(interface);

	//Binds the address-information to the socket
	err=bind(raw, (struct sockaddr*)&device, sizeof(device));
	//If the above operation gives an error.
	if(err == -1){
		perror("bind");
		close(raw);
		return -5;
	}

	ipc = socket(AF_UNIX, SOCK_SEQPACKET, 0);

	//Makes sure that the socket always can be re-used, if recently used for another connection
	activate=1;
	err = setsockopt(ipc, SOL_SOCKET, SO_REUSEADDR, &activate, sizeof(int));

	if(ipc == -1){
		perror("Socket");
		return -6;
	}
	
	daemonName = malloc(strlen(argv[1])+1);
	strcpy(daemonName, argv[1]);

	struct sockaddr_un bindaddr;
	bindaddr.sun_family = AF_UNIX;
	strncpy(bindaddr.sun_path, daemonName, sizeof(bindaddr.sun_path));

	if(bind(ipc, (struct sockaddr*)&bindaddr, sizeof(bindaddr)) == -1){
		perror("Bind");
		close(ipc);
		free(daemonName);
		return -7;
	}

	//Starts listening for connections on the request-socket. Max queue is set to 5.
	err=listen(ipc, 5);
	//If the above operation gives error.
	if(err == -1){
		perror("listen");
		close(ipc);
		free(daemonName);
		return -9;
	}

	first = malloc(sizeof (struct Arp_list));
	first->next=NULL;

	int fd_max;
	int i=0;

	if(raw > ipc)	fd_max = raw;
	else if (ipc > raw)	fd_max = ipc;

	//Runs the server
	while(i<maxCon){
		//initializes the FD_SETS
		FD_ZERO(&fds);

		FD_SET(raw, &fds);
		FD_SET(ipc, &fds);
		if(accpt!=0){
			FD_SET(accpt, &fds);
			fd_max=accpt;
		}
		
		//Checks if sockets are avalible.
		int sel=select(fd_max+1, &fds, NULL, NULL, NULL);
		//If the above operation gives error
		if(sel==-1){
			perror("select");
			close(raw);
			close(ipc);
			clearArp();
			free(daemonName);
			return -6;
		}

		//Looks for the socket we connected to earlier!
		if(FD_ISSET(accpt, &fds)){

			char buf[maxSize]={0};

			//closed socket
			if(!recIPC(accpt, buf)){

				close(accpt);
				accpt=0;
				if(raw > ipc)	fd_max=raw;
				else	fd_max=ipc;

			}else{

				//Håndter __ som skiller msg fra address
				char msg [maxSize]={0};
				char dst[1];
				decodeBuf(buf, msg, dst);
				
				if(retSet){
					dst[0]=ret[0];
					retSet=0;
				}

				#ifdef DEBUG
				printf("In accpt:\nmsg: %s. Len: %d\n", msg, (int)strlen(msg));
				printf("Dst: %d\n", (int)dst[0]);
				#endif

				size_t sndSize = strlen(msg);

				size_t msgsize = (sizeof(struct ether_frame) + sndSize + sizeof(struct MIP_Frame));

				struct ether_frame *frame = malloc(msgsize);
				struct MIP_Frame *mipFrame = malloc(sizeof(struct MIP_Frame) + sndSize);
				

				uint8_t mac[6];

				if(findArp(dst, mac)){
					//Create frames
					if(!setTransport(daemonName, dst, sndSize, msg, mipFrame)){
						//ERROR!
						printf("Error during framecreation(Transport)\n");
						close(raw);
						close(ipc);
						clearArp();

						if(frmSet)	free(tmpFrame);
						free(daemonName);
						free(frame);
						free(mipFrame);

						return -12;
					}

					createEtherFrame(mipFrame, sndSize, myAdr, mac, frame);
					
					//Send raw
					if(!sendRaw(raw, msgsize, frame)){
						perror("Error during raw sending");
						close(raw);
						close(ipc);
						clearArp();

						if(frmSet)	free(tmpFrame);
						free(daemonName);
						free(frame);
						free(mipFrame);
						return -11;
					}
					free(mipFrame);
					free(frame);
					
				} else{

					if(frmSet){
						free(tmpFrame);
					}

					tmpFrame = malloc((sizeof(struct MIP_Frame)) + sndSize);

					frmSet=1;

					uint8_t dst_addr[6];

					//Create arp-frame
					err = setARP(daemonName, mipFrame);

					//Create ether-frame
					memcpy(dst_addr, "\xFF\xFF\xFF\xFF\xFF\xFF", 6);

					createEtherFrame(mipFrame, 0, myAdr, dst_addr, frame);

					//SEND!
					if(!sendRaw(raw,(sizeof(struct ether_frame)+sizeof(struct MIP_Frame)), frame)){
						perror("Error during raw sending");
						close(raw);
						close(ipc);
						clearArp();
						free(tmpFrame);
						free(daemonName);
						free(frame);
						free(mipFrame);
						return -11;
					}
					free(mipFrame);
					free(frame);

					len=sndSize;

					if(!setTempTransp(daemonName, sndSize, msg, tmpFrame)){
						//ERROR!
						printf("Error during framecreation(TempTrans)!\n");
						close(raw);
						close(ipc);
						unlink(daemonName);
						clearArp();

						if(frmSet){
							free(tmpFrame);
						}
						free(daemonName);
						return -12;
					}
				}		
			}
		}

		//Checks if the request-socket is in the FD_SET.
		if(FD_ISSET(raw, &fds)){
			
			if(accpt == 0)	continue;

			char buf[1600];

			struct ether_frame *recvframe = (struct ether_frame*)buf;

			//Connected with a raw socket
			err=recRaw(raw, buf);
			if(!err){
				printf("Error while reciving from raw!\n");
				close(raw);
				close(ipc);
				clearArp();

				if(frmSet){
					free(tmpFrame);
				}
				
				free(daemonName);
				return -11;
			}


			struct MIP_Frame * recvdMIP = malloc(sizeof(recvframe->contents));
			recvdMIP = (struct MIP_Frame*) recvframe->contents;
			
			#ifdef DEBUG
				//Print information on message recieved
				//Sender+Reciever MIP-adress
				
				struct MIP_Frame* MIPu = malloc(sizeof(recvframe->contents));
				MIPu = (struct MIP_Frame*) recvframe->contents;

				//Print stuff for DEBUG mode!
				printf("\n\nDEBUG\n");
				printf("Destination address: ");
				printMAC(recvframe->dst_addr);
				printf("Source address:      ");
				printMAC(recvframe->src_addr);
				printf("Source MIPaddress: %d\n", (int) recvdMIP->srcMIP[0]);
				printf("Destination MIPaddress: %d\n", (int) recvdMIP->dstMIP[0]);
				printf("Protocol type:       %04x\n", ntohs(*((uint16_t*)recvframe->eth_proto)));
				if(MIPu->message[0] != '\0')	printf("Contents: %s\n", MIPu->message);
				else	printf("No content: ARP\n");	
			#endif

			//Create ethernet-frame !
			size_t msgsize; //= sizeof(struct ether_frame) + sizeof(struct MIP_Frame) + strlen(recvdMIP->message);
			struct ether_frame *frame; //= malloc(msgsize);

			err=findCase(recvdMIP);


			if (err==-1){
				printf("Faulty frame/frames recived!\n");
				close(raw);
				close(ipc);
				clearArp();

				if(frmSet){
					free(tmpFrame);
				}
				free(daemonName);
				free(recvdMIP);
				free(recvframe);
				return -8;
				//Recieved Arp-response.
			} else if(err == 2){
				//Save in Arp-cache

				struct MIP_Frame* MIP = malloc(sizeof(recvframe->contents));
				MIP = (struct MIP_Frame*) recvframe->contents;

				saveArp(MIP->srcMIP, recvframe->src_addr);

				//Finalize saved mip-frame!
				finalTransp(MIP->srcMIP, tmpFrame);

				msgsize= sizeof(struct ether_frame) + sizeof(struct MIP_Frame) + len;
				frame=malloc(msgsize);

				//Add everything to ethernet-frame & send!
				createEtherFrame(tmpFrame, len, myAdr, recvframe->src_addr, frame);

				//SEND
				if(!sendRaw(raw, msgsize, frame)){
					perror("Error during raw sending");
					close(raw);
					close(ipc);
					clearArp();

					if(frmSet){
						free(tmpFrame);
					}
					free(recvframe);
					free(daemonName);
					free(frame);
					free(recvdMIP);
					return -11;
				}

				free(frame);
				free(tmpFrame);
				frmSet=0;

			//Revieced Arp-request
			} else if(err == 3){
				uint8_t mac[6];

				struct MIP_Frame* MIP_IP = malloc(sizeof(recvframe->contents));
				MIP_IP = (struct MIP_Frame*) recvframe->contents;

				//Is it in my Arp-cache? If not, add!
				if(!findArp(MIP_IP->srcMIP, mac)){
					//Save in Arp-cache
					saveArp(MIP_IP->srcMIP, recvframe->src_addr);
				}

				//Send Arp-response!
				struct MIP_Frame* frm1 = malloc(sizeof(struct MIP_Frame));
				char tmp[1];
				tmp[0] = (int) recvdMIP->srcMIP[0];
				//Create Arp-response-frame.
				setARPReturn(daemonName, tmp, frm1);
				

				msgsize= sizeof(struct ether_frame) + sizeof(struct MIP_Frame);
				frame=malloc(msgsize);

				//Create ethernet-package
				createEtherFrame(frm1, 0, myAdr, recvframe->src_addr, frame);

				//SEND!
				if(!sendRaw(raw, msgsize, frame)){
					perror("Error during raw sending");
					close(raw);
					close(ipc);
					clearArp();

					if(frmSet){
						free(tmpFrame);
					}
					free(daemonName);
					free(frm1);
					free(frame);
					return -11;
				}

				free(frm1);
				free(frame);

			//Recived transport
			}else{
				//Send IPC

				struct MIP_Frame * MIP_IP = malloc(sizeof(recvframe->contents));
				MIP_IP = (struct MIP_Frame*) recvframe->contents;

				if(!sendIPC(accpt, MIP_IP->message)){
					perror("Error during IPC");
					close(raw);
					close(ipc);
					clearArp();

					if(frmSet){
						free(tmpFrame);
					}

					free(daemonName);
					return -9;
				}
			
				ret[0]=MIP_IP->srcMIP[0];
				retSet=1;
			}

			#ifdef DEBUG
			printf("\n");
			printArp();
			printf("END\n\n");
			#endif
		}
		
		if(FD_ISSET(ipc, &fds)){
			//Connected with ipc

			accpt = accept(ipc, NULL, NULL);
			if(accpt == -1){
				close(raw);
				close(ipc);
				clearArp();

				if(frmSet)	free(tmpFrame);
				free(daemonName);
				return -10;
			}
		}
		i++;
	}
	unlink(daemonName);
	free(daemonName);
	return 0;
}