#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <linux/sockios.h>

#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include <iwlib.h>
#include <popt.h>

/*
  LCDWifi 0.1.
  (C) Free Software Foundation 2004
  
  Author: Eskil Heyn Olsen (eskil at eskil dot org)
  
  Small utility to read (in a shitty way) a wifi cards signal strength, and
  display that plus a history graph for the strength.

  Compile ;
  g++ lcdwifi.cc -o lcdwifi -lpopt

  My VFD is 16 x 2 lines, a vbar is 0-7, change these defines, or alter the code to
  get them from the response from the "hello" to LCDd.
*/

char *g_lcdhost = "localhost:13666";
char *g_device = "eth0";
int g_lcd_sleep_time = 100;
int g_low_threshold = 45;
bool g_found_device = false;

wireless_config g_iwlib_dev_cfg;
unsigned long g_iw_bitrate;

int g_verbose = 0;


struct poptOption g_cli_options[] = {
    {"device", 'd', POPT_ARG_STRING, &g_device, 0, "specify device to monitor", "eth0"},
    {"lcdhost", 'h', POPT_ARG_STRING, &g_lcdhost, 0, "specify display host", "localhost:13666"},
    {"sleep", 's', POPT_ARG_INT, &g_lcd_sleep_time, 0, "ms to sleep after update", "100"},
    {"low", 'l', POPT_ARG_INT, &g_low_threshold, 0, "when link is less, blink", "45"},
    {"verbose", 'v', POPT_ARG_INT, &g_verbose, 0, "debugging verbosity", "0"},
    POPT_AUTOHELP
    {NULL, 0, 0, NULL, 0}
};


class LCD {
private:
	int m_fd;

	int connect_to_lcd_proc (const char *host, int port) {
		struct sockaddr_in sa;
		struct hostent *hp;
		
		int fd = socket (AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			perror ("socket");
			exit (1);
		}
		
		if (!(hp = gethostbyname(host))) {
			fprintf (stderr, "Cannot lookup %s\n", host);
			exit (1);
		}
		
		memset(&sa, 0, sizeof(struct sockaddr_in));
		sa.sin_port = htons(port);
		memcpy(&sa.sin_addr, hp->h_addr, hp->h_length);
		sa.sin_family = hp->h_addrtype;
		
		if (connect(fd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0) {
            fprintf (stderr, "connect to LCDd (%s:%d): %s\n", host, port, strerror (errno));
            exit (1);
		}
		
		return fd;
		
	}
public:
    char *m_lcdproc_version;
    char *m_lcdproc_protocol;
    int m_width, m_heigth, m_cell_width, m_cell_heigth;
  
	int send_message (char *format, ...) {
		char message[1024];
		va_list ap;
		
		va_start (ap, format);
		int mesg_sz = vsnprintf (message, sizeof (message)-1, format, ap);
		if (mesg_sz >= sizeof (message)-1) {
			fprintf (stderr, "need more than 1k message size...\n");
			exit (1);
		}
		
		int sent_sz;
		if ((sent_sz = write (m_fd, message, mesg_sz)) != mesg_sz) {
			perror ("write");
		}

        if (g_verbose > 5) printf ("LCDd snd : %s\n", message);

		write (m_fd, "\n", 1);
		va_end (ap);

        if (strcmp (message, "hello") == 0) {
            return read_response (true);
        }

		return read_response (false);
	}

	bool read_response (bool hello_response) {
		char message[1024];
		int read_sz = read (m_fd, message, 1024);
		char *end = strchr (message, '\n');
		*end = 0;

		if (g_verbose > 5) printf ("LCDd rcv : %s\n", message);

        if (hello_response) {
            return parse_hello (message);
        }

		return strcmp (message, "success") == 0;
	}

    bool matchToken (const char *msg, const char *token, const char **endptr) {
        while (msg && *msg && isspace (*msg)) msg++;
        if (strncmp (msg, token, strlen (token)) == 0) {
            *endptr = msg + strlen (token);
            while (**endptr && isspace (**endptr)) (*endptr)++;
            return true;
        }
        return false;
    }

    // really shitty parses...
    bool parse_hello (const char *msg) {
        const char *CONNECT = "connect";
        const char *PROTOCOL = "protocol";
        const char *LCDPROC = "LCDproc";
        const char *HEIGTH = "hgt";
        const char *WIDTH = "wid";
        const char *CELL_HEIGTH = "cellhgt";
        const char *CELL_WIDTH = "cellwid";
        const char *LCD = "lcd";
        const char *ptr = msg;    

        if (!matchToken (ptr, CONNECT, &ptr)) return false;
        while (ptr && *ptr) {

            const char *endptr;

            if (matchToken (ptr, LCDPROC, &endptr)) {
                const char *end = strchr (endptr, ' ');
                if (end == 0) end = endptr + strlen (endptr);
                m_lcdproc_version = strndup (endptr, end - endptr);
            } else if (matchToken (ptr, PROTOCOL, &endptr)) {
                const char *end = strchr (endptr, ' ');
                if (end == 0) end = endptr + strlen (endptr);
                m_lcdproc_protocol = strndup (endptr, end - endptr);
            } else if (matchToken (ptr, HEIGTH, &endptr)) {
                m_heigth = atol (endptr);
            } else if (matchToken (ptr, WIDTH, &endptr)) {
                m_width = atol (endptr);
            } else if (matchToken (ptr, CELL_HEIGTH, &endptr)) {
                m_cell_heigth = atol (endptr);
            } else if (matchToken (ptr, CELL_WIDTH, &endptr)) {
                m_cell_width = atol (endptr);
            } else if (matchToken (ptr, LCD, &endptr)) {
                ptr = endptr;
                continue;
            } else {
                fprintf (stderr, "unknown token from LCDproc hello response :\n%s\n", ptr);
            }

            if (endptr) {
                ptr = strchr (endptr, ' ');
            }
        }

        if (g_verbose) {
            printf ("LCDproc version %s\n", m_lcdproc_version);
            printf ("LCDproc protocol %s\n", m_lcdproc_protocol);
            printf ("LCD dimension %d x %d\n", m_width, m_heigth);
            printf ("LCD cell dimension %d x %d\n", m_cell_width, m_cell_heigth);
        }
    }

	LCD (const char *host, int port) {
        m_lcdproc_version = NULL;
        m_lcdproc_protocol = NULL;
		m_fd = connect_to_lcd_proc (host, port);
        if (g_verbose) {
            printf ("connecting to %s:%d, monitoring %s\n", host, port, g_device);
        }
		send_message ("hello");
		send_message ("screen_add wifi");
	}

    ~LCD () {
        if (m_lcdproc_version) {
            free (m_lcdproc_version);
        }
        if (m_lcdproc_protocol) {
            free (m_lcdproc_protocol);
        }
    }
};


class LCDWidget {
protected:
	LCD *m_lcd;
public:
	LCDWidget (LCD *lcd) : m_lcd (lcd) {
	}

	virtual ~LCDWidget () {};
};


typedef struct {
	char name[20];
	int val;
} GraphElem;


class SignalGraphWidget : public LCDWidget {
    int m_width;
protected:
    GraphElem *m_graph;  
public:
	SignalGraphWidget (LCD *lcd) : LCDWidget (lcd) {
        m_width = m_lcd->m_width - 4;
        m_graph = new GraphElem[m_width];
		for (int x = 0; x < m_width; x++) {
			snprintf (m_graph[x].name, sizeof (m_graph[x].name)-1, "wg%d", x);
			m_graph[x].val = 0;
			m_lcd->send_message ("widget_add wifi %s vbar", m_graph[x].name);
			m_lcd->send_message ("widget_set wifi %s %d 2 0", m_graph[x].name, x);
		}
	}
		
	virtual ~SignalGraphWidget () {
        delete[] m_graph;
	}

	void add_reading (int strength) {
		for (int x = 1; x < m_width; x++) {
			m_graph[x-1].val = m_graph[x].val;
		}
		int calibrated_strength = (int)rint (((double)(m_lcd->m_cell_heigth-1) * strength)/100);
		m_graph[m_width-1].val = calibrated_strength;
	}

	void update () {
        for (int x = m_width-1; x >=0; x--) {
			m_lcd->send_message ("widget_set wifi %s %d 2 %d", 
                                 m_graph[x].name, x, m_graph[x].val);
		}
	}
		
};


class SignalStrengthWidget : public LCDWidget {
protected:
    int m_str_len;
	char *m_str;
	SignalGraphWidget m_graph;
	int m_strength;
    int m_tx_load;
    int m_rx_load;
public:
	SignalStrengthWidget (LCD *lcd) : LCDWidget (lcd), m_graph (lcd) {
        m_str_len = m_lcd->m_width - 2;
        m_str = new char[m_str_len];

		m_lcd->send_message ("widget_add wifi mesg string");
		m_lcd->send_message ("widget_add wifi tx vbar");
		m_lcd->send_message ("widget_add wifi rx vbar");
	};

	~SignalStrengthWidget () {
        delete[] m_str;
	}

    void add_reading (int strength, int tx_load, int rx_load) {
		m_strength = strength;
        m_tx_load = tx_load;
        m_rx_load = rx_load;
		m_graph.add_reading (m_strength);
	}

	void update () {
        memset (m_str, ' ', m_str_len);
        m_str[m_str_len-1] = '\0';

		if (m_strength > g_low_threshold) {
			snprintf (m_str, m_str_len, "Signal %d%%", m_strength);
			m_lcd->send_message ("widget_set wifi mesg 1 1 \"%s\"", m_str);
        } else if (m_strength <= g_low_threshold && m_strength > 0) {
			m_lcd->send_message ("widget_set wifi mesg 1 1 \"%s\"", m_str);
			snprintf (m_str, m_str_len, "Signal %d%%", m_strength);
			m_lcd->send_message ("widget_set wifi mesg 1 1 \"%s\"", m_str);
        } else if (m_strength == 0) {
            m_lcd->send_message ("widget_set wifi mesg 1 1 \"%s\"", m_str);
            snprintf (m_str, m_str_len, "Lost signal", m_strength);
            m_lcd->send_message ("widget_set wifi mesg 1 1 \"%s\"", m_str);
		} else if (m_strength < 0) {
			snprintf (m_str, m_str_len, "No device %s", g_device);
			m_lcd->send_message ("widget_set wifi mesg 1 1 \"%s\"", m_str);
		}		

        int calibrated_tx = (int)rint (((double)(m_lcd->m_cell_heigth-1) * m_tx_load)/100);
        int calibrated_rx = (int)rint (((double)(m_lcd->m_cell_heigth-1) * m_rx_load)/100);

        // any activity, show at least 1 bar
        if (calibrated_tx == 0 && m_tx_load != 0) calibrated_tx = 1;
        if (calibrated_rx == 0 && m_rx_load != 0) calibrated_rx = 1;
        
        m_lcd->send_message ("widget_set wifi tx 13 1 %d", calibrated_tx);
        m_lcd->send_message ("widget_set wifi rx 14 1 %d", calibrated_rx);

        m_graph.update ();
	}
		
};


class ProcNetReader {
    char *m_device;
    int m_rate;

    int m_tx, m_rx, m_signal;
    int m_tx_load, m_rx_load;
  
    FILE *m_proc_net_wireless;
    FILE *m_proc_net_dev;

    void update_wireless () {
        rewind (m_proc_net_wireless);

        do {
            char line[256];
            char *ptr;
            char found_device[16];
      
            fgets (line, 256, m_proc_net_wireless);
      
            if (feof (m_proc_net_wireless)) {
                break;
            }
      
            if (line[6] == ':') {
                char *tptr = line;
        
                while (isspace (*tptr)) tptr++;
        
                strncpy (found_device, tptr, 6);
                (*strchr (found_device, ':')) = 0;
                ptr = line + 12;

                /* is it the one we're supposed to monitor ? */
                if (strcmp (found_device, g_device)==0) {
                    double link = strtod (ptr, &ptr);
                    ptr++;
          
                    double level = strtol (ptr, &ptr, 10);
                    ptr++;
          
                    double noise = strtol (ptr, &ptr, 10);
                    ptr++;
          
                    if (level < 0) {
                        m_signal = 0;
                    } else {
                        if (link<1) {
                            m_signal = 0;
                        } else {
                            m_signal = (int)rint ((log (link) / log (92)) * 100.0);
                        }
                    }
                    if (g_verbose > 3) {
                        printf ("signal strength %d %%\n", m_signal);
                    }

                    break;
                }
            }
        } while (1);
    }

    void update_rxtx () {
        static struct timeval last_time;
        struct timeval cur_time;
        rewind (m_proc_net_dev);

        do {
            char line[256];
            char *ptr;
            char found_device[16];
      
            fgets (line, 256, m_proc_net_dev);
            gettimeofday (&cur_time, 0);
      
            if (feof (m_proc_net_dev)) {
                break;
            }
      
            if (line[6] == ':') {
                char *tptr = line;
        
                while (isspace (*tptr)) tptr++;
        
                strncpy (found_device, tptr, 6);
                (*strchr (found_device, ':')) = 0;
                ptr = line + 8;


                /* is it the one we're supposed to monitor ? */
                if (strcmp (found_device, g_device)==0) {
                    int tx = m_tx;
                    int rx = m_rx;

                    // can't be bother to check...
                    sscanf (ptr, "%d %*d  %*d  %*d  %*d  %*d  %*d  %*d %d %*d  %*d  %*d  %*d  %*d  %*d  %*d", 
                            &m_rx, &m_tx);
          
                    double usecs = (cur_time.tv_sec - last_time.tv_sec) + ((double)(cur_time.tv_usec + (1000000 - last_time.tv_usec))/1000000);
                    rx = m_rx - rx;
                    tx = m_tx - tx;      
                    m_rx_load = (int)rint ((double)(rx * 100.0)/(usecs*m_rate));
                    m_tx_load = (int)rint ((double)(tx * 100.0)/(usecs*m_rate));

                    // sanity
                    if (m_rx_load > 100) m_rx_load = 100;
                    if (m_tx_load > 100) m_tx_load = 100;

                    if (g_verbose > 3) {
                        printf ("tx: %d KB in %f secs at %d KB/s = %d %%\n", 
                                tx/1024, usecs, m_rate/1024, 
                                (int)rint ((double)tx * (100.0/(usecs*m_rate))));
                        printf ("rx: %d KB in %f secs at %d KB/s = %d %%\n", 
                                rx/1024, usecs, m_rate/1024, 
                                (int)rint ((double)rx * (100.0/(usecs*m_rate))));
                    }
                }
            }
        } while (1);
        gettimeofday (&last_time, 0);
    }

public:
    ProcNetReader (const char *device, int rate) {
        m_device = strdup (device);
        m_tx = 0; 
        m_rx = 0; 
        m_signal = 0;
        m_rate = rate/8;

        m_proc_net_wireless =  fopen ("/proc/net/wireless", "rt");
        assert (m_proc_net_wireless);

        m_proc_net_dev =  fopen ("/proc/net/dev", "rt");
        assert (m_proc_net_dev);
    }
    ~ProcNetReader () {
        free (m_device);
    }
  
    int getSignalStrength () {
        return m_signal;
    }

    int getTxLoad () {
        return m_tx_load;
    }

    int getRxLoad () {
        return m_rx_load;
    }

    void update () {
        update_wireless ();
        update_rxtx ();
    }
};


int getInterfaces (int skfd, char *ifname, char *args[], int count) {
    if (strcmp (ifname, g_device) == 0) {
        if (iw_get_basic_config (skfd, ifname, &g_iwlib_dev_cfg) == 0) {
            struct iwreq iwr;
            strncpy(iwr.ifr_name, ifname, IFNAMSIZ);

            if (ioctl(skfd, SIOCGIWRATE, &iwr) >= 0) {
                g_iw_bitrate = iwr.u.bitrate.value;          
                if (g_verbose) {
                    printf ("device: %s, rate = %ld Kbps\n", ifname, g_iw_bitrate/1000);
                }
            }

            g_found_device = true;
        }
    }
}


void usage (const char *cmd) {
	printf ("usage: %s [host:port]\n", cmd);
}


int main (int argc, const char *argv[]) {
	char *host = "localhost";
	int port = 13666;
    int skfd;

	poptContext opt_con = poptGetContext (NULL, argc, argv, g_cli_options, 0);
	int opt_res = poptGetNextOpt (opt_con);
	poptFreeContext (opt_con);

	char *ptr;
	host = strdup (g_lcdhost);
	if ((ptr = strchr (host, ':')) != NULL) {
		*ptr = '\0';
		ptr++;
		port = atol (ptr);
	}


	LCD lcd (host, port);
	SignalStrengthWidget w (&lcd);

    skfd = iw_sockets_open ();
    iw_enum_devices (skfd, getInterfaces, NULL, 0);
    iw_sockets_close (skfd);

    if (!g_found_device) {
        fprintf (stderr, "%s isn't a wireless device\n", g_device);
        exit (1);
    }

    ProcNetReader reader (g_device, g_iw_bitrate);

    // FIXME: make method to get tx/rx bytes
    // FIXME: compare tx/rx bytes to rate, figure percentage, display graphs
    // FIXME: move to a method
    /* Here we begin to suck... */

	do {      
        reader.update ();
        w.add_reading (reader.getSignalStrength (), reader.getTxLoad (), reader.getRxLoad ());
        w.update ();

        usleep (g_lcd_sleep_time);
	} while (1);
}
