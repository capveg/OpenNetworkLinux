#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <opennsl/error.h>
#include <opennsl/field.h>
#include <sal/driver.h>

void usage(char * s1, char * s2) {
    if (s1)
        fprintf(stderr, "%s", s1);
    if (s2)
        fprintf(stderr, "%s", s2);
    if (s1 || s2)
        fprintf(stderr, "\n\n");
    fprintf(stderr, "Usage: opennsl-snake [p1:p2 [p3:p4 [...]]]\n"
            "    Copyright Big Switch Network 2016\n"
            "    Setup an Layer1 (forward all packets) connection between pairs of ports\n"
            "    Defaults to all ports, e.g., \"1:2 3:4 5:6 ...\"\n\n");
                
    exit(1);
}


/***
 * Send all traffic from port1 to port2 ; this is actually
 * asymetric - need to call independently for port2 --> port1
 * 
 */

#define ALLMASK 0xffffffff

int patch(int unit, opennsl_field_group_t group, int port1, int port2) {
    opennsl_field_entry_t entry;    // Holds all of the state for this patch rule
    int err;

    fprintf( stderr, "Patching Port %d to Port %d... ", port1, port2);
    // create the empty field entry data structure, tied to the group and chip/unit
    if ((err = opennsl_field_entry_create(unit, group, &entry)) != OPENNSL_E_NONE) {
        fprintf(stderr, "Entry Create Failed : %s \n", opennsl_errmsg(err));
        return -1;
    }

    // match if inport = port1
    if ((err = opennsl_field_qualify_InPort(unit, entry, port1, ALLMASK)) != OPENNSL_E_NONE ) {
        fprintf(stderr, "Qualify InPort failed : %s \n", opennsl_errmsg(err));
        return -1;
    }

    // redirect to port port2
    if ((err = opennsl_field_action_add(unit, entry,
                      opennslFieldActionRedirectPort, 0, port2)) != OPENNSL_E_NONE) {
        fprintf(stderr, "Field Action Add failed : %s \n", opennsl_errmsg(err));
        return -1;
    }

    // finially, insert the rule
    if ((err = opennsl_field_entry_install(unit, entry)) != OPENNSL_E_NONE) {
        fprintf(stderr, "Failed to install rule: %s\n", opennsl_errmsg(err));
        return -1;
    }

    fprintf( stderr, "done.\n");
    return 0;
}

/***
 * Setup the bare minimum on the switch
 *
 * For each port, enable forwarding
 *
 */

int switch_init( int unit) {

    opennsl_port_config_t pcfg;
    const int DEFAULT_VLAN=1;  // VLAB=1 exists by default, no need to create
    int rv;

    rv = opennsl_port_config_get(unit, &pcfg);
    if (rv != OPENNSL_E_NONE) {
        printf("Failed to get port configuration. Error %s\n", opennsl_errmsg(rv));
        return rv;
    }

    rv = opennsl_vlan_port_add(unit, DEFAULT_VLAN, pcfg.e, pcfg.e);
    if (rv != OPENNSL_E_NONE) {
        printf("Failed to add ports to VLAN. Error %s\n", opennsl_errmsg(rv));
        return rv;
    }

    return 0;
}   

/***
 * Setup a default snake port mapping
 */

void snake_all_ports(int * portmap, int n_ports) {
    int MAXPORTS_IS_EVEN = (n_ports %2) ==0;
    assert(MAXPORTS_IS_EVEN);
    for (int i=0; i<n_ports; i+=2) {
        portmap[i] = i+2;       // map port #1 to #2, #3 to #4
        portmap[i+1] = i+1;     // map port #2 to #1, #4 to #3
    }


}

#ifndef MAXPORTS
#define MAXPORTS 256
#endif


int main (int argc, char * argv[]) {
    opennsl_field_qset_t qset;      // The set of packet fields we want to qualify on
    opennsl_field_group_t group;    // Our matching group
    int err;
    int portmap[MAXPORTS];   
    int unit = 0;   // Assume single chip for everything - fix later
    
    memset(portmap, 0, sizeof(portmap));

    if (argc == 1)
        snake_all_ports(portmap, MAXPORTS);
    else {
        for (int i=1; i<argc; i++) {
            if ((argv[i][0] >= '0') && (argv[i][0]) <= '9') {
                // arg starts with a digit, try to parse as "1:2"
                int port_a, port_b;
                char * token = strtok(argv[i], ":");    // ignore the first token
                token = strtok(NULL, ":");
                if (token == NULL)
                    usage("Unknown port patch parameter (should be \"1:2\") -- ", argv[i]);
                port_a = atoi(argv[i]);
                port_b = atoi(token);
                if ((port_a <= 0) || (port_a >=MAXPORTS))
                    usage("Port out of range ", argv[i]);
                if ((port_b <= 0) || (port_b >=MAXPORTS))
                    usage("Port out of range ", token);
                portmap[port_a -1 ] = port_b;
                portmap[port_b -1 ] = port_a;
                fprintf(stderr, "Patching %d <---> %d\n", port_a, port_b);
            } else usage("Unknown parameter ", argv[i]);
        }
    }

    /**
     * Main initialization call to OpenNSL
     *
     * Do after as much arg parsing and safety checks as possible, because
     * this is slow.
     */
    if ((err = opennsl_driver_init(NULL)) != OPENNSL_E_NONE) {
        fprintf(stderr, "Failed to initialize OpenNSL: %d : %s\n",
                err, opennsl_errmsg(err));
        exit(1);
    }

    /** 
     * Set up some basic switch infrastrcuture 
     *
     */
    switch_init(unit);

    /**
     * Tell the chip we want the field processor to only match on input port
     */
    OPENNSL_FIELD_QSET_INIT( qset );
    OPENNSL_FIELD_QSET_ADD( qset, opennslFieldQualifyInPort );  // only match on input port

    // now see if the hardware will support this match (of course it will, it's trivial)
    if ((err = opennsl_field_group_create(unit, qset, OPENNSL_FIELD_GROUP_PRIO_ANY, &group)) != OPENNSL_E_NONE) {
        fprintf(stderr, "Failed to create the L1 matching group -- weird!?: %s \n", opennsl_errmsg(err));
        return -1;
    }



    /**
     * And now start patching!
     */
    for (int i=0; i<MAXPORTS; i++) {
        if (portmap[i] != 0)
            patch(unit, group, i+1, portmap[i]);
    }
    
    fprintf(stderr, "Forwarding packets; hit ^C to stop\n");
    while(1) 
        sleep(1);   // go into a slow, infinite loop; TODO print port stats here
    return 0;
}

