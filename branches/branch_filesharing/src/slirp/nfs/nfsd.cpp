#include <stdio.h>
#include <unistd.h>
#include <vector>

#include "nfsd.h"
#include "RPCServer.h"
#include "TCPServerSocket.h"
#include "UDPServerSocket.h"
#include "PortmapProg.h"
#include "NFSProg.h"
#include "MountProg.h"
#include "BootparamProg.h"
#include "configuration.h"
#include "SocketListener.h"
#include "VDNS.h"
#include "FileTable.h"

static bool         g_bLogOn = true;
static CPortmapProg g_PortmapProg;
static CRPCServer   g_RPCServer;

nfsd_NAT nfsd_ports = {{0},{0}};

static std::vector<UDPServerSocket*> SERVER_UDP;
static std::vector<TCPServerSocket*> SERVER_TCP;

FileTable* nfsd_fts[1]; // to be extended for multiple exports

static bool initialized = false;

static void add_program(CRPCProg *pRPCProg, uint16_t port = 0) {
    UDPServerSocket* udp = new UDPServerSocket(&g_RPCServer);
    TCPServerSocket* tcp = new TCPServerSocket(&g_RPCServer);
    
    g_RPCServer.Set(pRPCProg->GetProgNum(), pRPCProg);

    if (tcp->Open(pRPCProg->GetProgNum(), port) && udp->Open(pRPCProg->GetProgNum(), port)) {
        printf("[NFSD] %s started\n", pRPCProg->GetName());
        pRPCProg->Init(tcp->GetPort(), udp->GetPort());
        g_PortmapProg.Add(pRPCProg);
        SERVER_TCP.push_back(tcp);
        SERVER_UDP.push_back(udp);
    } else {
        printf("[NFSD] %s start failed.\n", pRPCProg->GetName());
    }
}

static void printAbout(void) {
    printf("[NFSD] Network File System server\n");
    printf("[NFSD] Copyright (C) 2005 Ming-Yang Kao\n");
    printf("[NFSD] Edited in 2011 by ZeWaren\n");
    printf("[NFSD] Edited in 2013 by Alexander Schneider (Jankowfsky AG)\n");
    printf("[NFSD] Edited in 2014 2015 by Yann Schepens\n");
    printf("[NFSD] Edited in 2016 by Peter Philipp (Cando Image GmbH), Marc Harding\n");
    printf("[NFSD] Mostly rewritten in 2019 by Simon Schubiger for Previous NeXT emulator\n");
}

extern "C" void nfsd_start(void) {
    nfsd_fts[0] = NULL;
    
    if(access(ConfigureParams.Ethernet.szNFSroot, F_OK | R_OK | W_OK) < 0) {
        printf("[NFSD] can not access directory '%s'. nfsd startup canceled.\n", ConfigureParams.Ethernet.szNFSroot);
        return;
    }
    
    char nfsd_hostname[_SC_HOST_NAME_MAX];
    gethostname(nfsd_hostname, sizeof(nfsd_hostname));
    
    printf("[NFSD] starting local NFS daemon on '%s', exporting '%s'\n", nfsd_hostname, ConfigureParams.Ethernet.szNFSroot);
    printAbout();
    
    if(initialized) return;
    
    nfsd_fts[0] = new FileTable(ConfigureParams.Ethernet.szNFSroot, "/netboot");

    static CNFSProg       NFSProg;
    static CMountProg     MountProg;
    static CBootparamProg BootparamProg;
    
	NFSProg.SetUserID(0, 0);
    
    g_RPCServer.SetLogOn(g_bLogOn);

    add_program(&g_PortmapProg, PORT_PORTMAP);
    add_program(&NFSProg,       PORT_NFS);
    add_program(&MountProg);
    add_program(&BootparamProg);
    
    static VDNS vdns;
    
    initialized = true;
}

extern "C" void nfsd_cleanup(void) {
}

extern "C" int nfsd_match_addr(uint32_t addr) {
    return (addr == (ntohl(special_addr.s_addr) | CTL_NFSD)) || (addr == (ntohl(special_addr.s_addr) | 0x00FFFFFF)); // NS kernel seems to braodcast on 10.255.255.255
}

extern "C" void nfsd_not_implemented(const char* file, int line) {
    printf("[NFSD] not implemented file:%s, line %d.", file, line);
    pause();
}

extern "C" FILE* nfsd_fopen(const char* path, const char* mode) {
    return nfsd_fts[0] ? nfsd_fts[0]->fopen(path, mode) : NULL;
}

extern "C" const char* nfsd_export_path(void) {
    static const char* exportPath = nfsd_fts[0]->GetBasePathAlias().c_str();
    return exportPath;
}
