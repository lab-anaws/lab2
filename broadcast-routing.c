/**
 * \file
 *         Contiki simple routing protocol implementation 
 * \author
 *         Carlo Vallati <c.vallati@iet.unipi.it>
 */


#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"

#include "simple-udp.h"


#include <stdio.h>
#include <string.h>

#include "lib/list.h"

#include "lib/memb.h"

#define MAX_IP	10

// List of IP addresses
struct ip_list_struct {
    struct ip_list_struct *next;
    uip_ipaddr_t ip;
};

LIST(ip_list);

MEMB(ip_mem, struct ip_list_struct, MAX_IP);

#define UDP_PORT 1234

#define SEND_INTERVAL		(10 * CLOCK_SECOND)
#define SEND_TIME		(random_rand() % (SEND_INTERVAL))

static struct simple_udp_connection broadcast_connection;

/*---------------------------------------------------------------------------*/
PROCESS(broadcast_example_process, "Simple routing implementation");
AUTOSTART_PROCESSES(&broadcast_example_process);
/*---------------------------------------------------------------------------*/

int 
list_exist(uip_ipaddr_t *item)
{
  struct ip_list_struct *s;
  for(s = list_head(ip_list);
      s != NULL;
      s = list_item_next(s)) {
    if(uip_ipaddr_cmp(item, &s->ip))
      return 1; // I wish you were here
  }
  
  return 0; // No luck  
}

static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  int i,num_neigh;
  uint8_t *ptr;
  struct ip_list_struct *s;
  uip_ipaddr_t *addr;
  uip_ds6_addr_t * nh;

  // Check if this is the first time I get a message from this node
  if( !list_exist(sender_addr) ){
     // First message from this neighbour
     s = memb_alloc(&ip_mem);
     uip_ipaddr_copy(&s->ip, sender_addr);
     list_add(ip_list, s);
     printf("Neighbour added \n"); 
  }

  printf("Neighbour information received from ");
  uip_debug_ipaddr_print(sender_addr);
  printf("\n");
 
  num_neigh = datalen/sizeof(uip_ipaddr_t);
  ptr=data;
  printf("2nd hop neighbour list is: \n");
  for(i=0; i < num_neigh; i++){
     ptr += i*sizeof(uip_ipaddr_t);
     addr = ptr;
     uip_debug_ipaddr_print(addr);

     // Add the 2nd neighbors in the routing table

     if(uip_ds6_is_my_addr(addr))
        continue; // Hey this is myself

     if( list_exist(addr) )
	continue; // Hey you're a 1st hop neighbour

     // I can add the entry in the routing table
     uip_ds6_route_add(addr, 128, sender_addr);
     printf("\nAdded route\n");

     // Verify!
     nh = uip_ds6_route_lookup(addr);
     if( nh != NULL ){
        printf("TO=");
        uip_debug_ipaddr_print(addr);
        printf(" NEXTHOP=");
        uip_debug_ipaddr_print(nh); 	
     }
  }
  printf("\n");



}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(broadcast_example_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;
  uip_ipaddr_t addr;
  char buf[sizeof(uip_ipaddr_t) * MAX_IP];
  char* ptr;
  int len;
  struct ip_list_struct *s;

  PROCESS_BEGIN();

  simple_udp_register(&broadcast_connection, UDP_PORT,
                      NULL, UDP_PORT,
                      receiver);

  list_init(ip_list);

  etimer_set(&periodic_timer, SEND_INTERVAL);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_reset(&periodic_timer);
    etimer_set(&send_timer, SEND_TIME);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));
    printf("Sending broadcast adv message\n");
    
    // Create the message with the list of mi neighbors
    ptr = &buf;
    len = 0;
    for(s = list_head(ip_list);
        s != NULL;
        s = list_item_next(s)) {
      uip_ipaddr_copy((uip_ipaddr_t*)ptr, &s->ip);
      ptr += sizeof(uip_ipaddr_t);
      len += sizeof(uip_ipaddr_t);
    }

    // An empty message is not delivered, make some padding and sent it!
    if(len == 0){
       buf[0] = 0;
       len++;
    }

    // Showtime, deliver the message
    uip_create_linklocal_allnodes_mcast(&addr);
    simple_udp_sendto(&broadcast_connection, &buf, len, &addr);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
