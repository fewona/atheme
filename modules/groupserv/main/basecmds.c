/* groupserv - group services.
 * Copyright (c) 2010 Atheme Development Group
 */

#include "groupserv_main.h"

/* I don't like this here, but it works --jdhore */
static void create_challenge(sourceinfo_t *si, const char *name, int v, char *dest)
{
	char buf[256];
	int digest[4];
	md5_state_t ctx;

	snprintf(buf, sizeof buf, "%lu:%s:%s",
			(unsigned long)(CURRTIME / 300) - v,
			get_source_name(si),
			name);
	md5_init(&ctx);
	md5_append(&ctx, (unsigned char *)buf, strlen(buf));
	md5_finish(&ctx, (unsigned char *)digest);
	/* note: this depends on byte order, but that's ok because
	 * it's only going to work in the same atheme instance anyway
	 */
	snprintf(dest, 80, "%x:%x", digest[0], digest[1]);
}

static void gs_cmd_drop(sourceinfo_t *si, int parc, char *parv[]);

command_t gs_drop = { "DROP", N_("Drops a group registration."), AC_AUTHENTICATED, 2, gs_cmd_drop, { .path = "groupserv/drop" } };

static void gs_cmd_drop(sourceinfo_t *si, int parc, char *parv[])
{
	mygroup_t *mg;
	char *name = parv[0];
	char *key = parv[1];
	char fullcmd[512];
	char key0[80], key1[80];

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "DROP");
		command_fail(si, fault_needmoreparams, _("Syntax: DROP <!group>"));
		return;
	}

	if (*name != '!')
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "DROP");
		command_fail(si, fault_badparams, _("Syntax: DROP <!group>"));
		return;
	}
	
	if (!(mg = mygroup_find(name)))
	{
		command_fail(si, fault_nosuch_target, _("Group \2%s\2 does not exist."), name);
		return;
	}

	if (!groupacs_sourceinfo_has_flag(mg, si, GA_FOUNDER))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to execute this command."));
		return;
	}

	if (si->su != NULL)
	{
		if (!key)
		{
			create_challenge(si, entity(mg)->name, 0, key0);
			snprintf(fullcmd, sizeof fullcmd, "/%s%s DROP %s %s",
					(ircd->uses_rcommand == false) ? "msg " : "",
					si->service->disp, entity(mg)->name, key0);
			command_success_nodata(si, _("To avoid accidental use of this command, this operation has to be confirmed. Please confirm by replying with \2%s\2"),
					fullcmd);
			return;
		}
		/* accept current and previous key */
		create_challenge(si, entity(mg)->name, 0, key0);
		create_challenge(si, entity(mg)->name, 1, key1);
		if (strcmp(key, key0) && strcmp(key, key1))
		{
			command_fail(si, fault_badparams, _("Invalid key for %s."), "DROP");
			return;
		}
	}

	logcommand(si, CMDLOG_REGISTER, "DROP: \2%s\2", entity(mg)->name);
	object_unref(mg);
	command_success_nodata(si, _("The group \2%s\2 has been dropped."), name);
	return;
}

static void gs_cmd_flags(sourceinfo_t *si, int parc, char *parv[]);

command_t gs_flags = { "FLAGS", N_("Sets flags on a user in a group."), AC_AUTHENTICATED, 3, gs_cmd_flags, { .path = "groupserv/flags" } };

static void gs_cmd_flags(sourceinfo_t *si, int parc, char *parv[])
{
	mowgli_node_t *n;
	mygroup_t *mg;
	myuser_t *mu;
	groupacs_t *ga;
	unsigned int flags = 0;
	unsigned int dir = 0;
	char *c;
	bool operoverride = false;

	if (!parv[0])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FLAGS");
		command_fail(si, fault_needmoreparams, _("Syntax: FLAGS <!group> [user] [changes]"));
		return;
	}

	if ((mg = mygroup_find(parv[0])) == NULL)
	{
		command_fail(si, fault_nosuch_target, _("The group \2%s\2 does not exist."), parv[0]);
		return;
	}
	
	if (!groupacs_sourceinfo_has_flag(mg, si, GA_FLAGS))
	{
		if (has_priv(si, PRIV_GROUP_AUSPEX))
			operoverride = true;
		else
		{
			command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
			return;
		}
	}

	if (!parv[1])
	{
		int i = 1;

		command_success_nodata(si, _("Entry Account                Flags"));
		command_success_nodata(si, "----- ---------------------- -----");

		MOWGLI_ITER_FOREACH(n, mg->acs.head)
		{
			ga = n->data;

			command_success_nodata(si, "%-5d %-22s %s", i, entity(ga->mu)->name,
					       gflags_tostr(ga_flags, ga->flags));

			i++;
		}

		command_success_nodata(si, "----- ---------------------- -----");
		command_success_nodata(si, _("End of \2%s\2 FLAGS listing."), parv[0]);

		if (operoverride)
			logcommand(si, CMDLOG_ADMIN, "FLAGS: \2%s\2 (oper override)", parv[0]);
		else
			logcommand(si, CMDLOG_GET, "FLAGS: \2%s\2", parv[0]);

		return;
	}

	/* simple check since it's already checked above */
	if (operoverride)
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}

	if ((mu = myuser_find_ext(parv[1])) == NULL)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not a registered account."), parv[1]);
		return;
	}

	if (!parv[2])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FLAGS");
		command_fail(si, fault_needmoreparams, _("Syntax: FLAGS <!group> <user> <changes>"));
		return;
	}

	ga = groupacs_find(mg, mu, 0);
	if (ga != NULL)
		flags = ga->flags;

	/* XXX: this sucks. :< We have to keep this here instead of using the "global" function
	 * because of the MU_NEVEROP check which is really only needed in FLAGS. 
	 */
	c = parv[2];
	while (*c)
	{
		switch(*c)
		{
		case '+':
			dir = 0;
			break;
		case '-':
			dir = 1;
			break;
		case '*':
			if (dir)
				flags = 0;
			else
				flags = GA_ALL;
			break;
		case 'F':
			if (dir)
				flags &= ~GA_FOUNDER;
			else
				flags |= GA_FOUNDER;
			break;
		case 'f':
			if (dir)
				flags &= ~GA_FLAGS;
			else
				flags |= GA_FLAGS;
			break;
		case 's':
			if (dir)
				flags &= ~GA_SET;
			else
				flags |= GA_SET;
			break;
		case 'v':
			if (dir)
				flags &= ~GA_VHOST;
			else
				flags |= GA_VHOST;
			break;
		case 'c':
			if (dir)
				flags &= ~GA_CHANACS;
			else
			{
				if (mu->flags & MU_NEVEROP)
				{
					command_fail(si, fault_noprivs, _("\2%s\2 does not wish to be added to channel access lists (NEVEROP set)."), entity(mu)->name);
					return;
				}
				flags |= GA_CHANACS;
			}
			break;
		case 'm':
			if (dir)
				flags &= ~GA_MEMOS;
			else
				flags |= GA_MEMOS;
			break;
		case 'b':
			if (dir)
				flags &= ~GA_BAN;
			else
				flags |= GA_BAN;
			break;
		default:
			break;
		}

		c++;
	}

	if (ga != NULL && flags != 0)
		ga->flags = flags;
	else if (ga != NULL)
	{
		groupacs_delete(mg, mu);
		command_success_nodata(si, _("\2%s\2 has been removed from \2%s\2."), entity(mu)->name, entity(mg)->name);
		logcommand(si, CMDLOG_SET, "FLAGS:REMOVE: \2%s\2 on \2%s\2", entity(mu)->name, entity(mg)->name);
		return;
	}
	else 
	{
		if (MOWGLI_LIST_LENGTH(&mg->acs) > maxgroupacs && (!(mg->flags & MG_ACSNOLIMIT)))
		{
			command_fail(si, fault_toomany, _("Group %s access list is full."), entity(mg)->name);
			return;
		}
		ga = groupacs_add(mg, mu, flags);
	}

	MOWGLI_ITER_FOREACH(n, entity(mg)->chanacs.head)
	{
		chanacs_t *ca = n->data;

		verbose(ca->mychan, "\2%s\2 now has flags \2%s\2 in the group \2%s\2 which communally has \2%s\2 on \2%s\2.",
			entity(mu)->name, gflags_tostr(ga_flags, ga->flags), entity(mg)->name,
			bitmask_to_flags(ca->level), ca->mychan->name);

		hook_call_channel_acl_change(ca);
	}

	command_success_nodata(si, _("\2%s\2 now has flags \2%s\2 on \2%s\2."), entity(mu)->name, gflags_tostr(ga_flags, ga->flags), entity(mg)->name);

	/* XXX */
	logcommand(si, CMDLOG_SET, "FLAGS: \2%s\2 now has flags \2%s\2 on \2%s\2", entity(mu)->name, gflags_tostr(ga_flags,  ga->flags), entity(mg)->name);
}

static void gs_cmd_regnolimit(sourceinfo_t *si, int parc, char *parv[]);

command_t gs_regnolimit = { "REGNOLIMIT", N_("Allow a group to bypass registration limits."), PRIV_GROUP_ADMIN, 2, gs_cmd_regnolimit, { .path = "groupserv/regnolimit" } };

static void gs_cmd_regnolimit(sourceinfo_t *si, int parc, char *parv[])
{
	mygroup_t *mg;

	if (!parv[0] || !parv[1])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "REGNOLIMIT");
		command_fail(si, fault_needmoreparams, _("Syntax: REGNOLIMIT <!group> <ON|OFF>"));
		return;
	}

	if ((mg = mygroup_find(parv[0])) == NULL)
	{
		command_fail(si, fault_nosuch_target, _("The group \2%s\2 does not exist."), parv[0]);
		return;
	}
	
	if (!strcasecmp(parv[1], "ON"))
	{
		if (mg->flags & MG_REGNOLIMIT)
		{
			command_fail(si, fault_nochange, _("\2%s\2 can already bypass registration limits."), entity(mg)->name);
			return;
		}

		mg->flags |= MG_REGNOLIMIT;

		wallops("%s set the REGNOLIMIT option on the group \2%s\2.", get_oper_name(si), entity(mg)->name);
		logcommand(si, CMDLOG_ADMIN, "REGNOLIMIT:ON: \2%s\2", entity(mg)->name);
		command_success_nodata(si, _("\2%s\2 can now bypass registration limits."), entity(mg)->name);
	}
	else if (!strcasecmp(parv[1], "OFF"))
	{
		if (!(mg->flags & MG_REGNOLIMIT))
		{
			command_fail(si, fault_nochange, _("\2%s\2 cannot bypass registration limits."), entity(mg)->name);
			return;
		}

		mg->flags &= ~MG_REGNOLIMIT;

		wallops("%s removed the REGNOLIMIT option from the group \2%s\2.", get_oper_name(si), entity(mg)->name);
		logcommand(si, CMDLOG_ADMIN, "REGNOLIMIT:OFF: \2%s\2", entity(mg)->name);
		command_success_nodata(si, _("\2%s\2 cannot bypass registration limits anymore."), entity(mg)->name);
	}
	else
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "REGNOLIMIT");
		command_fail(si, fault_badparams, _("Syntax: REGNOLIMIT <!group> <ON|OFF>"));
	}
}

static void gs_cmd_acsnolimit(sourceinfo_t *si, int parc, char *parv[]);

command_t gs_acsnolimit = { "ACSNOLIMIT", N_("Allow a group to bypass access list limits."), PRIV_GROUP_ADMIN, 2, gs_cmd_acsnolimit, { .path = "groupserv/acsnolimit" } };

static void gs_cmd_acsnolimit(sourceinfo_t *si, int parc, char *parv[])
{
	mygroup_t *mg;

	if (!parv[0] || !parv[1])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "ACSNOLIMIT");
		command_fail(si, fault_needmoreparams, _("Syntax: ACSNOLIMIT <!group> <ON|OFF>"));
		return;
	}

	if ((mg = mygroup_find(parv[0])) == NULL)
	{
		command_fail(si, fault_nosuch_target, _("The group \2%s\2 does not exist."), parv[0]);
		return;
	}
	
	if (!strcasecmp(parv[1], "ON"))
	{
		if (mg->flags & MG_ACSNOLIMIT)
		{
			command_fail(si, fault_nochange, _("\2%s\2 can already bypass access list limits."), entity(mg)->name);
			return;
		}

		mg->flags |= MG_ACSNOLIMIT;

		wallops("%s set the ACSNOLIMIT option on the group \2%s\2.", get_oper_name(si), entity(mg)->name);
		logcommand(si, CMDLOG_ADMIN, "ACSNOLIMIT:ON: \2%s\2", entity(mg)->name);
		command_success_nodata(si, _("\2%s\2 can now bypass access list limits."), entity(mg)->name);
	}
	else if (!strcasecmp(parv[1], "OFF"))
	{
		if (!(mg->flags & MG_ACSNOLIMIT))
		{
			command_fail(si, fault_nochange, _("\2%s\2 cannot bypass access list limits."), entity(mg)->name);
			return;
		}

		mg->flags &= ~MG_ACSNOLIMIT;

		wallops("%s removed the ACSNOLIMIT option from the group \2%s\2.", get_oper_name(si), entity(mg)->name);
		logcommand(si, CMDLOG_ADMIN, "ACSNOLIMIT:OFF: \2%s\2", entity(mg)->name);
		command_success_nodata(si, _("\2%s\2 cannot bypass access list limits anymore."), entity(mg)->name);
	}
	else
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "ACSNOLIMIT");
		command_fail(si, fault_badparams, _("Syntax: ACSNOLIMIT <!group> <ON|OFF>"));
	}
}

static void gs_cmd_join(sourceinfo_t *si, int parc, char *parv[]);

command_t gs_join = { "JOIN", N_("Join a open group."), AC_AUTHENTICATED, 2, gs_cmd_join, { .path = "groupserv/join" } };

static void gs_cmd_join(sourceinfo_t *si, int parc, char *parv[])
{
	mygroup_t *mg;
	groupacs_t *ga;
	metadata_t *md;
	unsigned int flags = 0;

	if (!parv[0])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "JOIN");
		command_fail(si, fault_needmoreparams, _("Syntax: JOIN <!groupname>"));
		return;
	}

	if (!(mg = mygroup_find(parv[0])))
	{
		command_fail(si, fault_alreadyexists, _("Group \2%s\2 does not exist."), parv[0]);
		return;
	}
	
	if (!(mg->flags & MG_OPEN))
	{
		command_fail(si, fault_noprivs, _("Group \2%s\2 is not open to anyone joining."), parv[0]);
		return;
	}

	if (groupacs_sourceinfo_has_flag(mg, si, GA_BAN))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to execute this command."));
		return;
	}

	if (groupacs_sourceinfo_has_flag(mg, si, 0))
	{
		command_fail(si, fault_nochange, _("You are already a member of group \2%s\2."), parv[0]);
		return;
	}

	if (MOWGLI_LIST_LENGTH(&mg->acs) > maxgroupacs && (!(mg->flags & MG_ACSNOLIMIT)))
        {
                command_fail(si, fault_toomany, _("Group %s access list is full."), entity(mg)->name);
                return;
        }

	if ((md = metadata_find(mg, "joinflags")))
		flags = atoi(md->value);
	else
		flags = gs_flags_parser(join_flags, 0);


	ga = groupacs_add(mg, si->smu, flags);
        
	command_success_nodata(si, _("You are now a member of \2%s\2."), entity(mg)->name);
}

static void gs_cmd_fdrop(sourceinfo_t *si, int parc, char *parv[]);

command_t gs_fdrop = { "FDROP", N_("Force drops a group registration."), PRIV_GROUP_ADMIN, 1, gs_cmd_fdrop, { .path = "groupserv/fdrop" } };

static void gs_cmd_fdrop(sourceinfo_t *si, int parc, char *parv[])
{
	mygroup_t *mg;
	char *name = parv[0];

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "DROP");
		command_fail(si, fault_needmoreparams, _("Syntax: DROP <!group>"));
		return;
	}

	if (*name != '!')
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "DROP");
		command_fail(si, fault_badparams, _("Syntax: DROP <!group>"));
		return;
	}

	if (!(mg = mygroup_find(name)))
	{
		command_fail(si, fault_nosuch_target, _("Group \2%s\2 does not exist."), name);
		return;
	}

	logcommand(si, CMDLOG_ADMIN | LG_REGISTER, "FDROP: \2%s\2", entity(mg)->name);
        wallops("%s dropped the group \2%s\2", get_oper_name(si), name);
	object_unref(mg);
	command_success_nodata(si, _("The group \2%s\2 has been dropped."), name);
	return;
}

static void gs_cmd_fflags(sourceinfo_t *si, int parc, char *parv[]);

command_t gs_fflags = { "FFLAGS", N_("Forces a flag change on a user in a group."), PRIV_GROUP_ADMIN, 3, gs_cmd_fflags, { .path = "groupserv/fflags" } };

static void gs_cmd_fflags(sourceinfo_t *si, int parc, char *parv[])
{
	mowgli_node_t *n;
	mygroup_t *mg;
	myuser_t *mu;
	groupacs_t *ga;
	unsigned int flags = 0;

	if (!parv[0] || !parv[1] || !parv[2])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FLAGS");
		command_fail(si, fault_needmoreparams, _("Syntax: FLAGS <!group> [user] [changes]"));
		return;
	}

	if ((mg = mygroup_find(parv[0])) == NULL)
	{
		command_fail(si, fault_nosuch_target, _("The group \2%s\2 does not exist."), parv[0]);
		return;
	}
	
        if (si->smu == NULL)
	{
		command_fail(si, fault_noprivs, _("You are not logged in."));
		return;
	}

	if ((mu = myuser_find_ext(parv[1])) == NULL)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not a registered account."), parv[1]);
		return;
	}

	ga = groupacs_find(mg, mu, 0);
	if (ga != NULL)
		flags = ga->flags;

	flags = gs_flags_parser(parv[2], 1);

	if (ga != NULL && flags != 0)
		ga->flags = flags;
	else if (ga != NULL)
	{
		groupacs_delete(mg, mu);
		command_success_nodata(si, _("\2%s\2 has been removed from \2%s\2."), entity(mu)->name, entity(mg)->name);
		wallops("\2%s\2 is removing flags for \2%s\2 on \2%s\2", get_oper_name(si), entity(mu)->name, entity(mg)->name);
		logcommand(si, CMDLOG_ADMIN, "FFLAGS:REMOVE: \2%s\2 on \2%s\2", entity(mu)->name, entity(mg)->name);
		return;
	}
	else 
	{
		if (MOWGLI_LIST_LENGTH(&mg->acs) > maxgroupacs && (!(mg->flags & MG_ACSNOLIMIT)))
		{
			command_fail(si, fault_toomany, _("Group %s access list is full."), entity(mg)->name);
			return;
		}
		ga = groupacs_add(mg, mu, flags);
	}

	MOWGLI_ITER_FOREACH(n, entity(mg)->chanacs.head)
	{
		chanacs_t *ca = n->data;

		verbose(ca->mychan, "\2%s\2 now has flags \2%s\2 in the group \2%s\2 which communally has \2%s\2 on \2%s\2.",
			entity(mu)->name, gflags_tostr(ga_flags, ga->flags), entity(mg)->name,
			bitmask_to_flags(ca->level), ca->mychan->name);

		hook_call_channel_acl_change(ca);
	}

	command_success_nodata(si, _("\2%s\2 now has flags \2%s\2 on \2%s\2."), entity(mu)->name, gflags_tostr(ga_flags, ga->flags), entity(mg)->name);

	/* XXX */
	wallops("\2%s\2 is modifying flags(\2%s\2) for \2%s\2 on \2%s\2", get_oper_name(si), gflags_tostr(ga_flags, ga->flags), entity(mu)->name, entity(mg)->name);
	logcommand(si, CMDLOG_ADMIN, "FFLAGS: \2%s\2 now has flags \2%s\2 on \2%s\2", entity(mu)->name, gflags_tostr(ga_flags,  ga->flags), entity(mg)->name);
}

void basecmds_init(void)
{
	service_bind_command(groupsvs, &gs_drop);
	service_bind_command(groupsvs, &gs_flags);
	service_bind_command(groupsvs, &gs_regnolimit);
	service_bind_command(groupsvs, &gs_acsnolimit);
	service_bind_command(groupsvs, &gs_join);
	service_bind_command(groupsvs, &gs_fdrop);
	service_bind_command(groupsvs, &gs_fflags);
}

void basecmds_deinit(void)
{
	service_unbind_command(groupsvs, &gs_drop);
	service_unbind_command(groupsvs, &gs_flags);
	service_unbind_command(groupsvs, &gs_regnolimit);
	service_unbind_command(groupsvs, &gs_acsnolimit);
	service_unbind_command(groupsvs, &gs_join);
	service_unbind_command(groupsvs, &gs_fdrop);
	service_unbind_command(groupsvs, &gs_fflags);
}
