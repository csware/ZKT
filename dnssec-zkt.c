/*****************************************************************
**
**	@(#) dnssec-zkt.c (c) Jan 2005  Holger Zuleger  hznet.de
**	Secure DNS zone key tool
**
**	See LICENCE file for licence
**
*****************************************************************/

# include <stdio.h>
# include <stdlib.h>	/* abort(), exit(), ... */
# include <string.h>
# include <dirent.h>
# include <assert.h>
# include <unistd.h>

# include "config.h"
#if defined(HAS_GETOPT_LONG) && HAS_GETOPT_LONG
# include <getopt.h>
#endif

# include "debug.h"
# include "misc.h"
# include "strlist.h"
# include "zconf.h"
# include "dki.h"

extern  int	optopt;
extern  int	opterr;
extern  int	optind;
extern  char	*optarg;
const	char	*progname;

char	*labellist = NULL;

int	headerflag = 1;
int	ageflag = 0;
int	timeflag = 1;
int	pathflag = 0;
int	kskflag = 1;
int	zskflag = 1;
int	ljustflag = 0;

static	int	dirflag = 0;
static	int	recflag = RECURSIVE;
static	int	trustedkeyflag = 0;
static	int	kskrollover = 0;
static	char	*kskdomain = "";

# define	short_options	":0:1:2:3:9A:C:D:P:R:HKTS:ZVac:dhkLl:prtz"
#if defined(HAS_GETOPT_LONG) && HAS_GETOPT_LONG
static struct option long_options[] = {
	{"ksk-rollover",	no_argument, NULL, '9'},
	{"ksk-status",		required_argument, NULL, '0'},
	{"ksk-roll-stat",	required_argument, NULL, '0'},
	{"ksk-newkey",		required_argument, NULL, '1'},
	{"ksk-publish",		required_argument, NULL, '2'},
	{"ksk-delkey",		required_argument, NULL, '3'},
	{"ksk-roll-phase1",	required_argument, NULL, '1'},
	{"ksk-roll-phase2",	required_argument, NULL, '2'},
	{"ksk-roll-phase3",	required_argument, NULL, '3'},
	{"list-dnskeys",	no_argument, NULL, 'K'},
	{"list-trustedkeys",	no_argument, NULL, 'T'},
	{"ksk",			no_argument, NULL, 'k'},
	{"zsk",			no_argument, NULL, 'z'},
	{"age",			no_argument, NULL, 'a'},
	{"time",		no_argument, NULL, 't'},
	{"recursive",		no_argument, NULL, 'r'},
	{"zone-config",		no_argument, NULL, 'Z'},
	{"leftjust",		no_argument, NULL, 'L'},
	{"path",		no_argument, NULL, 'p'},
	{"directory",		no_argument, NULL, 'd'},
	{"config",		required_argument, NULL, 'c'},
	{"pre-publish",		required_argument, NULL, 'P'},
	{"active",		required_argument, NULL, 'A'},
	{"depreciated",		required_argument, NULL, 'D'},
	{"create",		required_argument, NULL, 'C'},
	{"rename",		required_argument, NULL, 'R'},
	{"destroy",		required_argument, NULL, 20 },
	{"help",		no_argument, NULL, 'H'},
	{0, 0, 0, 0}
};
#endif

static	int	parsedirectory (const char *dir, dki_t **listp);
static	void	parsefile (const char *file, dki_t **listp);
static	void	createkey (const char *keyname, const dki_t *list, const zconf_t *conf);
static	void	ksk_rollover (const char *keyname, int phase, const dki_t *list, const zconf_t *conf);
static	int	create_parent_file (const char *fname, int phase, int ttl, const dki_t *dkp);
static	void    usage (char *mesg, zconf_t *cp);
static	const char *parsetag (const char *str, int *tagp);

main (int argc, char *argv[])
{
	dki_t	*data = NULL;
	dki_t	*dkp;
	int	c;
	int	opt_index;
	int	action;
	const	char	*file;
	char	*p;
	char	str[254+1];
	const char	*keyname;
	int		searchtag;
	zconf_t	*config;

	progname = *argv;
	if ( (p = strrchr (progname, '/')) )
		progname = ++p;

	config = loadconfig ("", (zconf_t *)NULL);	/* load built in config */
	if ( fileexist (CONFIG_FILE) )			/* load default config file */
		config = loadconfig (CONFIG_FILE, config);
	if ( config == NULL )
		fatal ("Out of memory\n");
	recflag = config->recursive;
	ageflag = config->printage;
	timeflag = config->printtime;
	ljustflag = config->ljust;

        opterr = 0;
	opt_index = 0;
#if defined(HAS_GETOPT_LONG) && HAS_GETOPT_LONG
	while ( (c = getopt_long (argc, argv, short_options, long_options, &opt_index)) != -1 )
#else
	while ( (c = getopt (argc, argv, short_options)) != -1 )
#endif
	{
		switch ( c )
		{
		case '9':		/* ksk rollover help */
			ksk_rollover ("help", c - '0', NULL, NULL);
			exit (1);
		case '1':		/* ksk rollover create new key */
		case '2':		/* ksk rollover publish DS */
		case '3':		/* ksk rollover delete old key */
		case '0':		/* ksk rollover status */
			action = c;
			if ( !optarg )
				usage ("ksk rollover requires an domain argument", config);
			kskdomain = optarg;
			break;
		case 'T':
			trustedkeyflag = 1;
			zskflag = pathflag = 0;
			/* fall through */
		case 'H':
		case 'K':
		case 'V':
		case 'Z':
			action = c;
			break;
		case 'C':
			pathflag = !pathflag;
			/* fall through */
		case 'P':
		case 'A':
		case 'D':
		case 'R':
		case 'S':
		case 20:
			if ( (keyname = parsetag (optarg, &searchtag)) != NULL )
			{
				int len = strlen (keyname);
				if ( len > 0 && keyname[len-1] != '.' )
				{
					snprintf (str, sizeof(str), "%s.", keyname);
					keyname = str;
				}
			}
			action = c;
			break;
		case 'a':		/* age */
			ageflag = !ageflag;
			break;
		case 'c':
			config = loadconfig (optarg, config);
			recflag = config->recursive;
			ageflag = config->printage;
			timeflag = config->printtime;
			ljustflag = config->ljust;
			checkconfig (config);
			break;
		case 'd':		/* ignore directory arg */
			dirflag = 1;
			break;
		case 'h':		/* print no headline */
			headerflag = 0;
			break;
		case 'k':		/* ksk only */
			zskflag = 0;
			break;
		case 'L':		/* ljust */
			ljustflag = !ljustflag;
			break;
		case 'l':		/* label list */
			labellist = prepstrlist (optarg, LISTDELIM);
			if ( labellist == NULL )
				fatal ("Out of memory\n");
			break;
		case 'p':		/* print path */
			pathflag = 1;
			break;
		case 'r':		/* switch recursive flag */
			recflag = !recflag;
			break;
		case 't':		/* time */
			timeflag = !timeflag;
			break;
		case 'z':		/* zsk only */
			kskflag = 0;
			break;
		case ':':
			snprintf (str, sizeof(str), "option \"-%c\" requires an argument.\n",
										optopt);
			usage (str, config);
			break;
		case '?':
			if ( isprint (optopt) )
				snprintf (str, sizeof(str), "Unknown option \"-%c\".\n",
										optopt);
			else
				snprintf (str, sizeof (str), "Unknown option char \\x%x.\n",
										optopt);
			usage (str, config);
			break;
		default:
			abort();
		}
	}

	/* it's better to do this before we read the whole directory tree */
	if ( action == 'Z' )
	{
		printconfig ("stdout", config);
		return 0;
	}

	if ( kskflag == 0 && zskflag == 0 )
		kskflag = zskflag = 1;

	c = optind;
	do {
		if ( c >= argc )		/* no args left */
			file = config->zonedir;	/* use default directory */
		else
			file = argv[c++];

		if ( is_directory (file) )
			parsedirectory (file, &data);
		else
			parsefile (file, &data);

	}  while ( c < argc );	/* for all arguments */

	switch ( action )
	{
	case 'H':
	case 'V':
		usage ("", config);
	case 'C':
		createkey (keyname, data, config);
		break;
	case 'P':
	case 'A':
	case 'D':
		if ( (dkp = (dki_t*)zkt_search (data, searchtag, keyname)) == NULL )
			fatal ("Key with tag %u not found\n", searchtag);
		else if ( dkp == (void *) 01 )
			fatal ("Key with tag %u found multiple times\n", searchtag);
		dki_setstatus_preservetime (dkp, action);
		break;
	case 20:
		if ( (dkp = (dki_t *)zkt_search (data, searchtag, keyname)) == NULL )
			fatal ("Key with tag %u not found\n", searchtag);
		else if ( dkp == (void *) 01 )
			fatal ("Key with tag %u found multiple times\n", searchtag);
		dki_destroy (dkp);
		break;
	case 'R':
		if ( (dkp = (dki_t *)zkt_search (data, searchtag, keyname)) == NULL )
			fatal ("Key with tag %u not found\n", searchtag);
		else if ( dkp == (void *) 01 )
			fatal ("Key with tag %u found multiple times\n", searchtag);
		dki_remove (dkp);
		break;
	case 'S':
		if ( (dkp = (dki_t *)zkt_search (data, searchtag, keyname)) == NULL )
			fatal ("Key with tag %u not found\n", searchtag);
		else if ( dkp == (void *) 01 )
			fatal ("Key with tag %u found multiple times\n", searchtag);
		dki_prt_dnskey (dkp, stdout);
		break;
	case 'K':
		zkt_list_dnskeys (data);
		break;
	case 'T':
		zkt_list_trustedkeys (data);
		break;
	case '1':	/* ksk rollover new key */
	case '2':	/* ksk rollover publish DS */
	case '3':	/* ksk rollover delete old key */
	case '0':	/* ksk rollover status */
		ksk_rollover (kskdomain, action - '0', data, config);
		break;
	default:
		zkt_list_keys (data);
	}

	return 0;
}

# define	sopt_usage(mesg, value)	fprintf (stderr, mesg, value)
#if defined(HAS_GETOPT_LONG) && HAS_GETOPT_LONG
# define	lopt_usage(mesg, value)	fprintf (stderr, mesg, value)
# define	loptstr(lstr, sstr)	lstr
#else
# define	lopt_usage(mesg, value)
# define	loptstr(lstr, sstr)	sstr
#endif
static	void    usage (char *mesg, zconf_t *cp)
{
        fprintf (stderr, "Secure DNS Zone Key Tool %s\n", ZKT_VERSION);
        fprintf (stderr, "\n");
        fprintf (stderr, "Show zone config parameter as %s file\n", LOCALCONF_FILE);
        sopt_usage ("\tusage: %s -Z\n", progname);
        lopt_usage ("\tusage: %s --zone-config\n", progname);
        fprintf (stderr, "\n");
        fprintf (stderr, "List keys in current or given directory (-r for recursive mode)\n");
        sopt_usage ("\tusage: %s [-dhatkzpr] [-c config] [file|dir ...]\n", progname);
        fprintf (stderr, "\n");
        fprintf (stderr, "List public part of keys in DNSKEY RR format\n");
        sopt_usage ("\tusage: %s -K [-dhkzr] [-c config] [file|dir ...]\n", progname);
        lopt_usage ("\tusage: %s --list-dnskeys [-dhkzr] [-c config] [file|dir ...]\n", progname);
        fprintf (stderr, "\n");
        fprintf (stderr, "List keys (output is suitable for trusted-keys section)\n");
        sopt_usage ("\tusage: %s -T [-dhzr] [-c config] [file|dir ...]\n", progname);
        lopt_usage ("\tusage: %s --list-trustedkeys [-dhzr] [-c config] [file|dir ...]\n", progname);
        fprintf (stderr, "\n");
        fprintf (stderr, "Create a new key \n");
        sopt_usage ("\tusage: %s -C <name> [-k] [-dpr] [-c config] [dir ...]\n", progname);
        lopt_usage ("\tusage: %s --create=<name> [-k] [-dpr] [-c config] [dir ...]\n", progname);
        fprintf (stderr, "\t\tKSK (use -k):  %s %d bits\n", dki_algo2str (cp->k_algo), cp->k_bits);
        fprintf (stderr, "\t\tZSK (default): %s %d bits\n", dki_algo2str (cp->z_algo), cp->z_bits);
        fprintf (stderr, "\n");
        fprintf (stderr, "Initiate a semi-automated KSK rollover");
        fprintf (stderr, "('%s -9%s' prints out a short description)\n", progname, loptstr ("|--ksk-rollover", ""));
        sopt_usage ("\tusage: %s {-1} do.ma.in.\n", progname);
        lopt_usage ("\tusage: %s {--ksk-roll-phase1|--ksk-newkey} do.ma.in.\n", progname);
        sopt_usage ("\tusage: %s {-2} do.ma.in.\n", progname);
        lopt_usage ("\tusage: %s {--ksk-roll-phase2|--ksk-publish} do.ma.in.\n", progname);
        sopt_usage ("\tusage: %s {-3} do.ma.in.\n", progname);
        lopt_usage ("\tusage: %s {--ksk-roll-phase3|--ksk-delkey} do.ma.in.\n", progname);
        sopt_usage ("\tusage: %s {-0} do.ma.in.\n", progname);
        lopt_usage ("\tusage: %s {--ksk-roll-stat|--ksk-status} do.ma.in.\n", progname);
        fprintf (stderr, "\n");
        fprintf (stderr, "Change key status of specified key to pre-publish, active or depreciated\n");
        fprintf (stderr, "\t(<keyspec> := tag | tag:name) \n");
        sopt_usage ("\tusage: %s -P|-A|-D <keyspec> [-dr] [-c config] [dir ...]\n", progname);
        lopt_usage ("\tusage: %s --pre-publish=<keyspec> [-dr] [-c config] [dir ...]\n", progname);
        lopt_usage ("\tusage: %s --active=<keyspec> [-dr] [-c config] [dir ...]\n", progname);
        lopt_usage ("\tusage: %s --depreciated=<keyspec> [-dr] [-c config] [dir ...]\n", progname);
        fprintf (stderr, "\n");
        fprintf (stderr, "Remove (rename) specified key (<keyspec> := tag | tag:name) \n");
        sopt_usage ("\tusage: %s -R <keyspec> [-dr] [-c config] [dir ...]\n", progname);
        lopt_usage ("\tusage: %s --rename=<keyspec> [-dr] [-c config] [dir ...]\n", progname);
        fprintf (stderr, "\n");
        fprintf (stderr, "Destroy specified key (<keyspec> := tag | tag:name) \n");
        lopt_usage ("\tusage: %s --destroy=<keyspec> [-dr] [-c config] [dir ...]\n", progname);

        fprintf (stderr, "\n");
        fprintf (stderr, "General options \n");
        fprintf (stderr, "\t-c file%s", loptstr (", --config=file\n", ""));
	fprintf (stderr, "\t\t read config from <file> instead of %s\n", CONFIG_FILE);
        fprintf (stderr, "\t-h\t no headline or trusted-key section header/trailer in -T mode\n");
        fprintf (stderr, "\t-d%s\t skip directory arguments\n", loptstr (", --directory", "\t"));
        fprintf (stderr, "\t-L%s\t print the domain name left justified (default: %s)\n", loptstr (", --leftjust", "\t"), ljustflag ? "on": "off");
        fprintf (stderr, "\t-l list\t\t print out only zone keys out of the given domain list\n");
        fprintf (stderr, "\t-p%s\t show path of keyfile / create key in current directory\n", loptstr (", --path", "\t"));
        fprintf (stderr, "\t-r%s\t recursive mode on/off (default: %s)\n", loptstr(", --recursive", "\t"), recflag ? "on": "off");
        fprintf (stderr, "\t-a%s\t print age of key (default: %s)\n", loptstr (", --age", "\t"), ageflag ? "on": "off");
        fprintf (stderr, "\t-t%s\t print key generation time (default: %s)\n", loptstr (", --time", "\t"),
								timeflag ? "on": "off");
        fprintf (stderr, "\t-k%s\t key signing keys only\n", loptstr (", --ksk", "\t"));
        fprintf (stderr, "\t-z%s\t zone signing keys only\n", loptstr (", --ksk", "\t"));
        if ( mesg && *mesg )
                fprintf (stderr, "%s\n", mesg);
        exit (1);
}

static	void	createkey (const char *keyname, const dki_t *list, const zconf_t *conf)
{
	const char *dir = "";
	dki_t	*dkp;

	if ( keyname == NULL || *keyname == '\0' )
		fatal ("Create key: no keyname!");

	dbg_val2 ("createkey: keyname %s, pathflag = %d\n", keyname, pathflag);
	/* search for already existent key to get the directory name */
	if ( pathflag && (dkp = (dki_t *)zkt_search (list, 0, keyname)) != NULL )
	{
		char    path[MAX_PATHSIZE+1];
		zconf_t localconf;

		dir = dkp->dname;
		pathname (path, sizeof (path), dir, LOCALCONF_FILE, NULL);
		if ( fileexist (path) )                 /* load local config file */
		{
			dbg_val ("Load local config file \"%s\"\n", path);
			memcpy (&localconf, conf, sizeof (zconf_t));
			conf = loadconfig (path, &localconf);
		}
	}
	
	if  ( zskflag )
		dkp = dki_new (dir, keyname, 0, conf->z_algo, conf->z_bits, conf->z_random);
	else
		dkp = dki_new (dir, keyname, 1, conf->k_algo, conf->k_bits, conf->k_random);
	if ( dkp == NULL )
		fatal ("Can't create key %s: %s!\n", keyname, dki_geterrstr ());
	if  ( zskflag )
		dki_setstatus (dkp, 'p');
}

static	int	get_parent_phase (const char *file)
{
	FILE	*fp;
	int	phase;

	if ( (fp = fopen (file, "r")) == NULL )
		return -1;

	phase = 0;
	if ( fscanf (fp, "; KSK rollover phase%d", &phase) != 1 )
		phase = 0;

	fclose (fp);
	return phase;
}

static	void	ksk_rollover (const char *keyname, int phase, const dki_t *list, const zconf_t *conf)
{
	char    path[MAX_PATHSIZE+1];
	zconf_t localconf;
	const char *dir;
	dki_t	*keylist;
	dki_t	*dkp;
	int	parent_exist;
	int	parent_age;
	int	parent_phase;
	int	parent_propagation;
	int	key_ttl;
	int	ksk;

	if ( phase == 9 )	/* usage */
	{
		fprintf (stderr, "A KSK rollover requires three consecutive steps:\n");
		fprintf (stderr, "\n");
		fprintf (stderr, "-1%s", loptstr ("|--ksk-roll-phase1 (--ksk-newkey)\n", ""));
		fprintf (stderr, "\t Create a new KSK.\n");
		fprintf (stderr, "\t This step also creates a parent-<domain> file which contains only\n");
		fprintf (stderr, "\t the _old_ key.  This file will be copied in hierarchical mode\n");
		fprintf (stderr, "\t by dnssec-signer to the parent directory as keyset-<domain> file.\n");
		fprintf (stderr, "\t Wait until the new keyset is propagated, before going to the next step.\n");
		fprintf (stderr, "\n");
		fprintf (stderr, "-2%s", loptstr ("|--ksk-roll-phase2 (--ksk-publish)\n", ""));
		fprintf (stderr, "\t This step creates a parent-<domain> file with the _new_ key only.\n");
		fprintf (stderr, "\t Please send this file immediately to the parent (In hierarchical\n");
		fprintf (stderr, "\t mode this will be done automatically by the dnssec-signer command).\n");
		fprintf (stderr, "\t Then wait until the new DS is generated by the parent and propagated\n");
		fprintf (stderr, "\t to all the parent name server, plus the old DS TTL before going to step three.\n");
		fprintf (stderr, "\n");
		fprintf (stderr, "-3%s", loptstr ("|--ksk-roll-phase3 (--ksk-delkey)\n", ""));
		fprintf (stderr, "\t Remove (rename) the old KSK and the parent-<domain> file.\n");
		fprintf (stderr, "\t You have to manually delete the old KSK (look at file names beginning\n");
		fprintf (stderr, "\t with an lower 'k').\n");
		fprintf (stderr, "\n");
		fprintf (stderr, "-0%s", loptstr ("|--ksk-roll-stat (--ksk-status)\n", ""));
		fprintf (stderr, "\t Show the current KSK rollover state of a domain.\n");

		fprintf (stderr, "\n");

		return;
	}

	if ( keyname == NULL || *keyname == '\0' )
		fatal ("ksk rollover: no domain!");

	dbg_val2 ("ksk_rollover: keyname %s, phase = %d\n", keyname, phase);

	/* search for already existent key to get the directory name */
	if ( (keylist = (dki_t *)zkt_search (list, 0, keyname)) == NULL )
		fatal ("ksk rollover: domain %s not found!\n", keyname);
	dkp = keylist;

	/* try to read local config file */
	dir = dkp->dname;
	pathname (path, sizeof (path), dir, LOCALCONF_FILE, NULL);
	if ( fileexist (path) )                 /* load local config file */
	{
		dbg_val ("Load local config file \"%s\"\n", path);
		memcpy (&localconf, conf, sizeof (zconf_t));
		conf = loadconfig (path, &localconf);
	}
	key_ttl = conf->key_ttl;

	/* check if parent-file already exist */
	pathname (path, sizeof (path), dir, "parent-", keyname);
	parent_phase = parent_age = 0;
	if ( (parent_exist = fileexist (path)) != 0 )
	{
		parent_phase = get_parent_phase (path);
		parent_age = file_age (path);
	}
	// parent_propagation = 2 * DAYSEC;
	parent_propagation = 5 * MINSEC;

	ksk = 0;	/* count key signing keys */
	for ( dkp = keylist; dkp; dkp = dkp->next )
		if ( dki_isksk (dkp) )
			ksk++;


	switch ( phase )
	{
	case 0:	/* print status (debug) */
		fprintf (stdout, "ksk_rollover:\n");
		fprintf (stdout, "\t domain = %s\n", keyname);
		fprintf (stdout, "\t phase = %d\n", parent_phase);
		fprintf (stdout, "\t parent_file %s %s\n", path, parent_exist ? "exist": "not exist");
		if ( parent_exist )
			fprintf (stdout, "\t age of parent_file %d %s\n", parent_age, str_delspace (age2str (parent_age)));
		fprintf (stdout, "\t # of ksk %d\n", ksk);
		fprintf (stdout, "\t parent_propagation %d %s\n", parent_propagation, str_delspace (age2str (parent_propagation)));
		fprintf (stdout, "\t keys ttl %d %s\n", key_ttl, age2str (key_ttl));

		for ( dkp = keylist; dkp; dkp = dkp->next )
		{
			/* TODO: Nur zum testen */
			dki_prt_dnskey (dkp, stdout);
		}
		break;
	case 1:
		if ( parent_exist || ksk > 1 )
			fatal ("Can\'t create new ksk because there is already an ksk rollover in progress\n");

		fprintf (stdout, "create new ksk \n");
		dkp = dki_new (dir, keyname, 1, conf->k_algo, conf->k_bits, conf->k_random);
		if ( dkp == NULL )
			fatal ("Can't create key %s: %s!\n", keyname, dki_geterrstr ());
		dkp = keylist;	/* use old key to create the parent file */
		if ( !create_parent_file (path, phase, key_ttl, dkp) )
			fatal ("Couldn't create parentfile %s\n", path);
		break;

	case 2:
		if ( ksk < 2 )
			fatal ("Can\'t publish new key because no one exist\n");
		if ( !parent_exist )
			fatal ("More than one KSK but no parent file found!\n");
		if ( parent_phase != 1 )
			fatal ("Parent file exists but is in wrong state (phase = %d)\n", parent_phase);
		if ( parent_age < conf->proptime + key_ttl )
			fatal ("ksk_rollover (phase2): you have to wait for propagation of the new KSK (at least %dsec or %s)\n",
				conf->proptime + key_ttl - parent_age,
				str_delspace (age2str (conf->proptime + key_ttl - parent_age)));

		fprintf (stdout, "save new ksk in parent file\n");
		dkp = keylist->next;	/* set dkp to new ksk */
		if ( !create_parent_file (path, phase, key_ttl, dkp) )
			fatal ("Couldn't create parentfile %s\n", path);
		break;
	case 3:
		if ( !parent_exist || ksk < 2 )
			fatal ("ksk-delkey only allowed after ksk-publish\n");
		if ( parent_phase != 2 )
			fatal ("Parent file exists but is in wrong state (phase = %d)\n", parent_phase);
		if ( parent_age < parent_propagation + key_ttl )
			fatal ("ksk_rollover (phase3): you have to wait for DS propagation  (at least %dsec or %s)\n",
				parent_propagation + key_ttl - parent_age,
				str_delspace (age2str (parent_propagation + key_ttl - parent_age)));
		/* remove the parentfile */
		fprintf (stdout, "remove parentfile \n");
		unlink (path);
		/* remove or rename the old key */
		fprintf (stdout, "old ksk renamed \n");
		dkp = keylist;	/* set dkp to old ksk */
		dki_remove (dkp);
		break;
	default:	assert (phase == 1 || phase == 2 || phase == 3);
	}
}

/*****************************************************************
**	create_parent_file ()
*****************************************************************/
static	int	create_parent_file (const char *fname, int phase, int ttl, const dki_t *dkp)
{
	char	*p;
	FILE	*fp;

	if ( dkp == NULL || (phase != 1 && phase != 2) )
		return 0;

	if ( (fp = fopen (fname, "w")) == NULL )
		fatal ("can\'t create new parentfile \"%s\"\n", fname);

	if ( phase == 1 )
		fprintf (fp, "; KSK rollover phase1 (old key)\n");
	else
		fprintf (fp, "; KSK rollover phase2 (new key)\n");

	fprintf (fp, "%s ", dkp->name);
	if ( ttl > 0 )
		fprintf (fp, "%d ", ttl);
	fprintf (fp, "IN DNSKEY  ");
	fprintf (fp, "%d 3 %d (", dkp->flags, dkp->algo);
	fprintf (fp, "\n\t\t\t"); 
	for ( p = dkp->pubkey; *p ; p++ )
		if ( *p == ' ' )
			fprintf (fp, "\n\t\t\t"); 
		else
			putc (*p, fp);
	fprintf (fp, "\n\t\t) ; key id = %u\n", dkp->tag); 
	fclose (fp);

	return phase;
}

static	int	parsedirectory (const char *dir, dki_t **listp)
{
	dki_t	*dkp;
	DIR	*dirp;
	struct  dirent  *dentp;
	char	path[MAX_PATHSIZE+1];

	if ( dirflag )
		return 0;

	dbg_val ("directory: opendir(%s)\n", dir);
	if ( (dirp = opendir (dir)) == NULL )
		return 0;

	while ( (dentp = readdir (dirp)) != NULL )
	{
		if ( is_dotfile (dentp->d_name) )
			continue;

		dbg_val ("directory: check %s\n", dentp->d_name);
		pathname (path, sizeof (path), dir, dentp->d_name, NULL);
		if ( is_directory (path) && recflag )
		{
			dbg_val ("directory: recursive %s\n", path);
			parsedirectory (path, listp);
		}
		else if ( is_keyfilename (dentp->d_name) )
			if ( (dkp = dki_read (dir, dentp->d_name)) )
			{
				// fprintf (stderr, "parsedir: tssearch (%d %s)\n", dkp, dkp->name);
				dki_t	**p;
#if defined (USE_TREE) && USE_TREE
				dki_tadd (listp, dkp);
#else
				dki_add (listp, dkp);
#endif
			}
	}
	closedir (dirp);
	return 1;
}

static	void	parsefile (const char *file, dki_t **listp)
{
	char	path[MAX_PATHSIZE+1];
	dki_t	*dkp;

	/* file arg contains path ? ... */
	file = splitpath (path, sizeof (path), file);	/* ... then split of */

	if ( is_keyfilename (file) )	/* plain file name looks like DNS key file ? */
	{
		if ( (dkp = dki_read (path, file)) )	/* read DNS key file ... */
#if defined (USE_TREE) && USE_TREE
			dki_tadd (listp, dkp);		/* ... and add to tree */
#else
			dki_add (listp, dkp);		/* ... and add to list */
#endif
		else
			error ("error parsing %s: (%s)\n", file, dki_geterrstr());
	}
}

static	const char *parsetag (const char *str, int *tagp)
{
	const	char	*p;

	*tagp = 0;
	while ( isspace (*str) )	/* skip leading ws */
		str++;

	p = str;
	if ( isdigit (*p) )		/* keytag starts with digit */
	{
		sscanf (p, "%u", tagp);	/* read keytag as number */
		do			/* eat up to the end of the number */
			p++;
		while ( isdigit (*p) );

		if ( *p == ':' )	/* label follows ? */
			return p+1;	/* return that */
		if ( *p == '\0' )
			return NULL;	/* no label */
	}
	return str;	/* return as label string if not a numeric keytag */
}

