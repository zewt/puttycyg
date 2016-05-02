#include "putty.h"
#include "dialog.h"

void cygterm_setup_config_box(struct controlbox *b, int midsession)
{
    union control *c;
    int i;
    struct controlset *s;
    s = ctrl_getset(b, "Session", "hostport",
                    "Specify the destination you want to connect to");
    for (i = 0; i < s->ncontrols; i++) {
	c = s->ctrls[i];
    }
    if (!midsession) {
	ctrl_settitle(b, "Connection/Cygterm",
	              "Options controlling Cygterm sessions");
	s = ctrl_getset(b, "Connection/Cygterm", "cygterm",
	                "Configure Cygwin paths");
	ctrl_checkbox(s, "Autodetect Cygwin installation", 'd',
	              HELPCTX(no_help),
	              dlg_stdcheckbox_handler,
	              I(offsetof(Config,cygautopath)));
    }
}

/* ex:set ts=8 sw=4: */
