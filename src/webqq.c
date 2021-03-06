#define PURPLE_PLUGINS

#include <plugin.h>
#include <version.h>
#include <smemory.h>
#include <request.h>
#include <signal.h>
#include <accountopt.h>
#include <ft.h>
#include <imgstore.h>

#include <type.h>
#include <async.h>
#include <msg.h>
#include <info.h>
#include <http.h>
#include <login.h>
#include <lwdb.h>
#include <utility.h>

#include "qq_types.h"
#include "translate.h"
#include "remote.h"
#include "cgroup.h"
#include "lwdb.h"

#define OPEN_URL(var,url) snprintf(var,sizeof(var),"xdg-open '%s'",url);

char *qq_get_cb_real_name(PurpleConnection *gc, int id, const char *who);
static void client_connect_signals(PurpleConnection* gc);
static void group_member_list_come(qq_account* ac,LwqqGroup* group);
//static int group_message_delay_display(qq_account* ac,LwqqGroup* group,const char* sender,const char* buf,time_t t);
static void whisper_message_delay_display(qq_account* ac,LwqqGroup* group,char* from,char* msg,time_t t);
static void friend_avatar(qq_account* ac,LwqqBuddy* buddy);
static void group_avatar(LwqqAsyncEvent* ev,LwqqGroup* group);
static void login_stage_1(LwqqClient* lc,LwqqErrorCode err);
static void login_stage_2(LwqqAsyncEvset* set,LwqqClient* lc);
static void add_friend_receipt(LwqqAsyncEvent* ev);
static void show_confirm_table(LwqqClient* lc,LwqqConfirmTable* table);
static void qq_login(PurpleAccount *account);
static void add_friend(LwqqClient* lc,LwqqConfirmTable* c,LwqqBuddy* b,char* message);

enum ResetOption{
    RESET_BUDDY=0x1,
    RESET_GROUP=0x2,
    RESET_DISCU=0x4,
    RESET_GROUP_SOFT=0x8,///<this only delete duplicated chat
    RESET_ALL=RESET_BUDDY|RESET_GROUP|RESET_DISCU
};
    

///###  global data area ###///
int g_ref_count = 0;
///###  global data area ###///

static const char* serv_id_to_local(qq_account* ac,const char* serv_id)
{
    if(ac->flag & QQ_USE_QQNUM){
        LwqqBuddy* buddy = find_buddy_by_uin(ac->qq,serv_id);
        return (buddy&&buddy->qqnumber) ?buddy->qqnumber:serv_id;
    }else
        return serv_id;
}

static const char* local_id_to_serv(qq_account* ac,const char* local_id)
{
    if(ac->flag & QQ_USE_QQNUM){
        LwqqBuddy* buddy = find_buddy_by_qqnumber(ac->qq,local_id);
        return (buddy&&buddy->uin)?buddy->uin:local_id;
    }else return local_id;
}

static const char* qq_get_type_from_chat(PurpleChat* chat)
{
    GHashTable* table = purple_chat_get_components(chat);
    return g_hash_table_lookup(table,QQ_ROOM_TYPE);
}
static LwqqGroup* qq_get_group_from_chat(PurpleChat* chat)
{
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    LwqqClient* lc = ac->qq;
    if(!lwqq_client_valid(lc)) return NULL;
    LwqqGroup* ret = NULL;
    GHashTable* table = purple_chat_get_components(chat);
    const char* key = g_hash_table_lookup(table,QQ_ROOM_KEY_GID);
    ret = find_group_by_qqnumber(lc, key);
    if(ret == NULL)
        ret = find_group_by_gid(lc,key);
    return ret;
}
//#define get_name_from_chat(chat) (g_hash_table_lookup(purple_chat_get_components(chat),QQ_ROOM_KEY_GID));
static const char* get_name_from_chat(PurpleChat* chat)
{
    GHashTable* table = purple_chat_get_components(chat);
    return g_hash_table_lookup(table,QQ_ROOM_KEY_GID);
}
static LwqqGroup* find_group_by_name(LwqqClient* lc,const char* name)
{
    LwqqGroup* group = NULL;
    LIST_FOREACH(group,&lc->groups,entries) {
        if(group->name&&strcmp(group->name,name)==0)
            return group;
    }
    LIST_FOREACH(group,&lc->discus,entries) {
        if(group->name&&strcmp(group->name,name)==0)
            return group;
    }
    return NULL;
}
static LwqqSimpleBuddy* find_group_member_by_nick_or_card(LwqqGroup* group,const char* who)
{
    if(!group || !who ) return NULL;
    LwqqSimpleBuddy* sb;
    LIST_FOREACH(sb,&group->members,entries) {
        if(sb->nick&&strcmp(sb->nick,who)==0)
            return sb;
        if(sb->card&&strcmp(sb->card,who)==0)
            return sb;
    }
    return NULL;
}
static LwqqSimpleBuddy* find_discu_member_by_nick(LwqqGroup* group,const char* who)
{
    LwqqSimpleBuddy* sb;
    LIST_FOREACH(sb,&group->members,entries) {
        if(sb->nick&&strcmp(sb->nick,who)==0)
            return sb;
    }
    return NULL;
}

static int find_group_and_member_by_card(LwqqClient* lc,const char* card,LwqqGroup** p_g,LwqqSimpleBuddy** p_sb)
{
    if(!card) return 0;
    char nick[128]={0};
    char gname[128]={0};
    const char* pos;
    if((pos = strstr(card," ### "))!=NULL) {
        strcpy(gname,pos+strlen(" ### "));
        strncpy(nick,card,pos-card);
        nick[pos-card] = '\0';
        *p_g = find_group_by_name(lc,gname);
        *p_sb = find_group_member_by_nick_or_card(*p_g,nick);
        return 1;
    }
    return 0;
}
static void action_about_webqq(PurplePluginAction *action)
{
    PurpleConnection *gc = (PurpleConnection *) action->context;
    GString *info;
    gchar *title;

    g_return_if_fail(NULL != gc);

    info = g_string_new("<html><body>");
    g_string_append(info, "<p>"
            "<b>Author</b>:<br>xiehuc<br>"
            "<b>GitCommit</b>:<br>"GIT_SHA1"<br>"
            "</p>"
            );

    g_string_append(info, "pidgin-lwqq mainly referenced: "
                            "1.openfetion for libpurple about<br/>"
                            "2.lwqq for webqq about<br/>"
                            "so it remaind a easy job<br/>"
                            "thanks riegamaths@gmail.com's great guide");
    g_string_append(info, "<br/><br/></body></html>");
    title = g_strdup_printf(_("About pidgin-lwqq %s"), DISPLAY_VERSION);
    purple_notify_formatted(gc, title, title, NULL, info->str, NULL, NULL);

    g_free(title);
    g_string_free(info, TRUE);
}
static void visit_self_infocenter(PurplePluginAction *action)
{
    PurpleConnection* gc = action->context;
    qq_account* ac = purple_connection_get_protocol_data(gc);
    char url[256]={0};
    snprintf(url,sizeof(url),"xdg-open 'http://user.qzone.qq.com/%s/infocenter'",ac->qq->myself->uin);
    system(url);
}
static void user_help(PurplePluginAction* action)
{
    char cmd[512];
    //the web page of user help;
    OPEN_URL(cmd,_("https://github.com/xiehuc/pidgin-lwqq/wiki/simple-user-guide"));
    system(cmd);
}
static void faq(PurplePluginAction* action)
{
    char cmd[512];
    //the web page of faq
    OPEN_URL(cmd,_("https://github.com/xiehuc/pidgin-lwqq/wiki/FAQ"));
    system(cmd);
}
static void feedback(PurplePluginAction* action)
{
    char cmd[512];
    //the web page of feedback
    OPEN_URL(cmd,_("https://github.com/xiehuc/pidgin-lwqq/issues"));
    system(cmd);
}
static void buddies_all_remove(void* data,void* userdata)
{
    PurpleBuddy* buddy = data;
    qq_account* ac = userdata;
    if(purple_buddy_get_account(buddy) == ac->account) {
        purple_blist_remove_buddy(buddy);
    }
}
//this remove all buddy and chat.
static void all_reset(qq_account* ac,int opt)
{
    if(opt & RESET_BUDDY){
        GSList* buddies = purple_blist_get_buddies();
        g_slist_foreach(buddies,buddies_all_remove,ac);
    }

    if(opt & (RESET_GROUP | RESET_DISCU |RESET_GROUP_SOFT)){
        //PurpleGroup* group = purple_find_group("QQ群");
        PurpleBlistNode *group,*node;
        for(group = purple_get_blist()->root;group!=NULL;group=group->next){
            node = group->child;
            while(node!=NULL) {
                if(PURPLE_BLIST_NODE_IS_CHAT(node)) {
                    PurpleChat* chat = PURPLE_CHAT(node);
                    if(purple_chat_get_account(chat)==ac->account) {
                        node = purple_blist_node_next(node,1);
                        const char* type = qq_get_type_from_chat(chat);
                        if(type==NULL||!strcmp(type,QQ_ROOM_TYPE_QUN)){
                            if(opt & RESET_GROUP) purple_blist_remove_chat(chat);
                            if(opt & RESET_GROUP_SOFT ){
                                const char* name = get_name_from_chat(chat);
                                if(lwqq_group_find_group_by_qqnumber(ac->qq,name)==NULL)
                                    purple_blist_remove_chat(chat);
                            }
                        }else{
                            if(opt & RESET_DISCU) purple_blist_remove_chat(chat);
                        }
                        continue;
                    }
                }
                node = purple_blist_node_next(node,1);
            }
        }
    }
}
static void all_reset_action(PurplePluginAction* action)
{
    PurpleConnection* gc = action->context;
    qq_account* ac = purple_connection_get_protocol_data(gc);

    all_reset(ac,RESET_ALL);

    purple_connection_error_reason(gc,PURPLE_CONNECTION_ERROR_OTHER_ERROR,_("all reloaded,please relogin"));
}
#if 0
static void visit_my_qq_center(PurplePluginAction* action)
{
    PurpleConnection* gc = action->context;
    qq_account* ac = purple_connection_get_protocol_data(gc);
    //char buf[1024]={0};
    //snprintf(buf,sizeof(buf),"xdg-open 'http://ptlogin2.qq.com/login?u=2501542492&p=146FA572EB4E2E1251BB197D7125E630&verifycode=!DGM&aid=1006102&u1=http5%%3A%%2F%%2Fid.qq.com%%2Findex.html%23myfriends&h=1&ptredirect=1&ptlang=2052&from_ui=1&dumy=&fp=loginerroralert&action=2-5-10785&mibao_css=&t=1&g=1'");
    char* test = NULL;
    lwqq_service_login(ac->qq, "", test);
    //system(buf);
    //qq_remote_call(ac, "http://id.qq.com/#myfriends");
    //LwqqString str;
    //snprintf(buf,sizeof(buf),"xdg-open '%s'",str.str);
    //system(buf);



    //system("xdg-open 'http://id.qq.com/#myfriends'");

}
#endif

static void add_group_receipt(LwqqAsyncEvent* ev,LwqqGroup* g)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    qq_account* ac = lc->data;
    if(err == 6 ){
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,_("Error Message"),_("ErrCode:6\nPlease try agagin later\n"),NULL,NULL,NULL);
    }
    lwqq_group_free(g);
}
static void add_group(LwqqClient* lc,LwqqConfirmTable* c,LwqqGroup* g)
{
    if(c->answer == LWQQ_NO){
        goto done;
    }
    LwqqAsyncEvent* ev = lwqq_info_add_group(lc, g, c->input);
    lwqq_async_add_event_listener(ev, _C_(2p,add_group_receipt,ev,g));
done:
    lwqq_ct_free(c);
}
static void search_group_receipt(LwqqAsyncEvent* ev,LwqqGroup* g)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    qq_account* ac = lc->data;
    if(err == WEBQQ_FATAL){
        LwqqAsyncEvent* event = lwqq_info_search_group_by_qq(lc,g->qq,g);
        lwqq_async_add_event_listener(event, _C_(2p,search_group_receipt,ev,g));
        return;
    }
    if(err == LWQQ_EC_NO_RESULT){
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,_("Error Message"),_("Get QQ Group Infomation Failed"),NULL,NULL,NULL);
        lwqq_group_free(g);
        return;
    }
    LwqqConfirmTable* confirm = s_malloc0(sizeof(*confirm));
    confirm->title = s_strdup(_("Confirm QQ Group"));
    confirm->input_label = s_strdup(_("Additional Reason"));
    char body[1024] = {0};
#define ADD_INFO(k,v)  format_append(body,_(k":%s\n"),v)
    ADD_INFO("QQ",g->qq);
    ADD_INFO("Name",g->name);
#undef ADD_INFO
    confirm->body = s_strdup(body);
    confirm->cmd = _C_(3p,add_group,lc,confirm,g);
    show_confirm_table(lc,confirm);
}

static void search_group(qq_account* ac,const char* text)
{
    LwqqGroup* g = lwqq_group_new(LWQQ_GROUP_QUN);
    LwqqAsyncEvent* ev;
    ev = lwqq_info_search_group_by_qq(ac->qq, text, g);
    lwqq_async_add_event_listener(ev, _C_(2p,search_group_receipt,ev,g));
}
static void do_no_thing(void* data,const char* text)
{
}
static void qq_add_group(PurplePluginAction* action)
{
    PurpleConnection* gc = action->context;
    qq_account* ac = purple_connection_get_protocol_data(gc);


    purple_request_input(gc,_("Add QQ Group"), _("QQ Number"), NULL, NULL, FALSE, FALSE, NULL, 
            _("Search"), G_CALLBACK(search_group), _("Cancel"), G_CALLBACK(do_no_thing), ac->account, NULL, NULL, ac);
}
static void create_discu(qq_account* ac,PurpleRequestFields* root)
{
    LwqqClient* lc = ac->qq;
    const char* name = purple_request_fields_get_string(root, "name");
    char* members = s_strdup(purple_request_fields_get_string(root, "members"));
    char* ptr = members;
    char* piece;
    LwqqDiscuMemChange* chg = lwqq_discu_mem_change_new();
    while((piece = strsep(&ptr,";"))){
        LwqqBuddy* b = find_buddy_by_qqnumber(lc, piece);
        if(b == NULL) b = lwqq_buddy_find_buddy_by_name(lc, piece);
        lwqq_discu_add_buddy(chg, b);
    }
    lwqq_info_create_discu(lc, chg, name);
    s_free(members);
}
static void qq_create_discu(PurplePluginAction* action)
{
    PurpleConnection* gc = action->context;
    qq_account* ac = purple_connection_get_protocol_data(gc);

    PurpleRequestField* leaf;
    PurpleRequestFields* root = purple_request_fields_new();
    PurpleRequestFieldGroup* branch = purple_request_field_group_new("");
    purple_request_fields_add_group(root, branch);
    leaf = purple_request_field_string_new("name", _("Discussion Name"), _("UnName Discussion"), FALSE);
    purple_request_field_string_set_editable(leaf, TRUE);
    purple_request_field_group_add_field(branch, leaf);
    leaf = purple_request_field_string_new("members", _("members(seperate by ;)"), "", TRUE);
    purple_request_field_string_set_editable(leaf, TRUE);
    purple_request_field_group_add_field(branch, leaf);

    purple_request_fields(gc, _("Create Discussion"), NULL, NULL, root, _("Create"), G_CALLBACK(create_discu), _("Cancel"), G_CALLBACK(do_no_thing), ac->account, NULL, NULL, ac);
}
#if 0
static void qq_open_recent(PurplePluginAction* action)
{
    PurpleConnection* gc = action->context;
    LwqqBuddy* buddy = action->user_data;
    PurpleConversation* conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, buddy->qqnumber, gc->account);
    if(conv == NULL) purple_conversation_new(PURPLE_CONV_TYPE_IM, gc->account, buddy->qqnumber);
}
#endif

#if 0 
static void add_buddies_to_talkgroup(PurpleConnection* gc,GList* row,void* data)
{
}

static void close_add_buddies_to_talkgroup(void* ac)
{
}

static void search_result_test(PurplePluginAction* act)
{
    PurpleConnection* gc = act->context;
    PurpleNotifySearchResults* res = purple_notify_searchresults_new();
    purple_notify_searchresults_column_add(res, purple_notify_searchresults_column_new("QQ"));
    purple_notify_searchresults_column_add(res, purple_notify_searchresults_column_new("name"));
    purple_notify_searchresults_button_add(res, PURPLE_NOTIFY_BUTTON_ADD, add_buddies_to_talkgroup);
    GList* l = NULL;
    l = g_list_append(l,s_strdup("123456"));
    l = g_list_append(l,s_strdup("测试"));
    purple_notify_searchresults_row_add(res, l);
    purple_notify_searchresults(gc, "添加成员", NULL, NULL, res, close_add_buddies_to_talkgroup, gc->proto_data);
}
#endif

static void modify_self_longnick(LwqqClient* lc,LwqqConfirmTable* ct)
{
    if(ct->answer == LWQQ_YES){
        const char* longnick = ct->input;
        lwqq_info_set_self_long_nick(lc, longnick);
    }
    lwqq_ct_free(ct);
}

static void display_self_longnick(LwqqClient* lc)
{
    LwqqConfirmTable* ct = s_malloc0(sizeof(*ct));
    ct->title = s_strdup(_("Modify Self Longnick"));
    ct->input_label = s_strdup(_("Longnick"));
    ct->input = s_strdup(lc->myself->long_nick);
    ct->cmd = _C_(2p,modify_self_longnick,lc,ct);
    show_confirm_table(lc, ct);
}

static void qq_modify_self_longnick(PurplePluginAction* act)
{
    PurpleConnection* gc = act->context;
    qq_account* ac = gc->proto_data;
    LwqqClient* lc = ac->qq;

    LwqqAsyncEvent* ev = lwqq_info_get_single_long_nick(lc, lc->myself);
    lwqq_async_add_event_listener(ev, _C_(p,display_self_longnick,lc));
}

static GList *plugin_actions_menu(PurplePlugin *UNUSED(plugin), gpointer context)
{

    GList *m = NULL;
    PurplePluginAction *act;
    //PurpleConnection* gc = context;

    ///分割线
    m = g_list_append(m, NULL);

    act = purple_plugin_action_new(_("About pidgin-lwqq"), action_about_webqq);
    m = g_list_append(m, act);
    act = purple_plugin_action_new(_("User Help"),user_help);
    m = g_list_append(m, act);
    act = purple_plugin_action_new(_("FAQ"), faq);
    m = g_list_append(m, act);
    act = purple_plugin_action_new(_("Feedback"), feedback);
    m = g_list_append(m, act);
    act = purple_plugin_action_new(_("Self Center"),visit_self_infocenter);
    m = g_list_append(m, act);
    //act = purple_plugin_action_new("好友管理",visit_my_qq_center);
    //m = g_list_append(m, act);
    act = purple_plugin_action_new(_("Add QQ Group"),qq_add_group);
    m = g_list_append(m, act);
    act = purple_plugin_action_new(_("Create Discussion Group"),qq_create_discu);
    m = g_list_append(m, act);
    act = purple_plugin_action_new(_("Modify Self LongNick"),qq_modify_self_longnick);
    m = g_list_append(m, act);
    act = purple_plugin_action_new(_("All Reload(Debug)"),all_reset_action);
    m = g_list_append(m, act);

    return m;
}


static const char *qq_list_icon(PurpleAccount *UNUSED(a), PurpleBuddy *UNUSED(b))
{
    return "webqq";
}

static GList *qq_status_types(PurpleAccount *UNUSED(account))
{
    PurpleStatusType *status;
    GList *types = NULL;
#define WEBQQ_STATUS_TYPE_ATTR \
    TRUE,TRUE,FALSE,\
    "nick","nick",purple_value_new(PURPLE_TYPE_STRING),\
    "mark","mark",purple_value_new(PURPLE_TYPE_STRING),NULL


    status = purple_status_type_new_with_attrs(PURPLE_STATUS_AVAILABLE,
    //status = purple_status_type_new_with_attrs(PURPLE_STATUS_MOBILE,
             "online", _("Online"), WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);
    status = purple_status_type_new_with_attrs(PURPLE_STATUS_AVAILABLE,
             "callme",_("Callme"),WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);
    status = purple_status_type_new_with_attrs(PURPLE_STATUS_AWAY,
             "away", _("Away"), WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);
    status = purple_status_type_new_with_attrs(PURPLE_STATUS_UNAVAILABLE,
             "busy", _("Busy"), WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);
    status = purple_status_type_new_with_attrs(PURPLE_STATUS_UNAVAILABLE,
             "slience", _("Slience"), WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);
    status = purple_status_type_new_with_attrs(PURPLE_STATUS_INVISIBLE,
             "hidden", _("Hidden"), WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);
    status = purple_status_type_new_with_attrs(PURPLE_STATUS_OFFLINE,
             "offline", _("Offline"), WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);
    status = purple_status_type_new_with_attrs(PURPLE_STATUS_MOBILE,
             "mobile", _("Online"), WEBQQ_STATUS_TYPE_ATTR);
    types = g_list_append(types, status);

#undef WEBQQ_STATUS_TYPE_ATTR
    return types;
}
static void qq_set_status(PurpleAccount* account,PurpleStatus* status)
{
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    lwqq_info_change_status(ac->qq,lwqq_status_from_str(purple_status_get_id(status)));
}

#define buddy_status(bu) ((bu->stat == LWQQ_STATUS_ONLINE && bu->client_type == LWQQ_CLIENT_MOBILE) \
        ? "mobile":lwqq_status_to_str(bu->stat))

static void friend_come(LwqqClient* lc,LwqqBuddy* buddy)
{
    qq_account* ac = lwqq_client_userdata(lc);
    ac->disable_send_server = 1;
    PurpleAccount* account=ac->account;
    PurpleBuddy* bu = NULL;
    LwqqFriendCategory* cate;

    int cate_index = buddy->cate_index;
    PurpleGroup* group = NULL;
    if(cate_index == LWQQ_FRIEND_CATE_IDX_DEFAULT){
        group = purple_group_new(QQ_DEFAULT_CATE);
    }else if(cate_index == LWQQ_FRIEND_CATE_IDX_PASSERBY){
        group = purple_group_new(QQ_PASSERBY_CATE);
    }else if(cate_index != 0) {
        LIST_FOREACH(cate,&lc->categories,entries) {
            if(cate->index==cate_index) {
                group = purple_group_new(cate->name);
                break;
            }
        }
    }

    const char* key = try_get(buddy->qqnumber,buddy->uin);
    const char* disp = try_get(buddy->markname,buddy->nick);
    bu = purple_find_buddy(account,key);
    if(bu == NULL) {
        bu = purple_buddy_new(ac->account,key,(buddy->markname)?buddy->markname:buddy->nick);
        purple_blist_add_buddy(bu,NULL,group,NULL);
        //if there isn't a qqnumber we shouldn't save it.
        if(!buddy->qqnumber) purple_blist_node_set_flags(PURPLE_BLIST_NODE(bu),PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    }
    purple_buddy_set_protocol_data(bu, buddy);
    buddy->data = bu;
    if(purple_buddy_get_group(bu)!=group&&strcmp(purple_buddy_get_group(bu)->name,ac->recent_group_name)!=0) 
        purple_blist_add_buddy(bu,NULL,group,NULL);
    if(!bu->alias || strcmp(bu->alias,disp)!=0 )
        purple_blist_alias_buddy(bu,disp);
    if(buddy->stat){
        if(buddy->long_nick)
            purple_prpl_got_user_status(account, key, buddy_status(buddy), "long_nick",buddy->long_nick,NULL);
        else
            purple_prpl_got_user_status(account, key, buddy_status(buddy), NULL);
    }
    //this is avaliable when reload avatar in
    //login_stage_f
    if(buddy->avatar_len)
        friend_avatar(ac, buddy);
    //download avatar 
    PurpleBuddyIcon* icon;
    if((icon = purple_buddy_icons_find(account,key))==0) {
        LwqqAsyncEvent* ev = lwqq_info_get_friend_avatar(lc,buddy);
        lwqq_async_add_event_listener(ev,_C_(2p,friend_avatar,ac,buddy));
    }

    qq_account_insert_index_node(ac, buddy,NULL);

    ac->disable_send_server = 0;
}

static void qq_set_group_name(qq_chat_group* cg)
{
    PurpleChat * chat = cg->chat;
    char gname[256] = {0};
    int hide = cg->group->mask > 0;
    if(hide) strcat(gname,"(");
    strcat(gname,cg->group->markname?:cg->group->name);
    if(hide){
        strcat(gname,")");
        unsigned int unread = CGROUP_UNREAD(cg);
        unsigned int split = unread<10?unread:unread<100?unread/10*10:unread/100*100;
        if(unread>0)sprintf(gname+strlen(gname), "(%u%s)",split,unread>10?"+":"");
    }
    purple_blist_alias_chat(chat, gname);
}

static struct qq_chat_group_opt qq_cg_opt_default = {
    .new_msg_notice = qq_set_group_name
};
#define QQ_CG_OPT &qq_cg_opt_default

static void group_come(LwqqClient* lc,LwqqGroup* group)
{
    qq_account* ac = lwqq_client_userdata(lc);
    ac->disable_send_server = 1;
    PurpleAccount* account=ac->account;
    PurpleGroup* qun = purple_group_new(QQ_GROUP_DEFAULT_CATE);
    PurpleGroup* talk = purple_group_new(_("Discussion"));
    GHashTable* components;
    PurpleChat* chat;
    const char* type;
    const char* key = try_get(group->account,group->gid);

    if( (chat = purple_blist_find_chat(account,key)) == NULL) {
        components = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_hash_table_insert(components,g_strdup(QQ_ROOM_KEY_GID),g_strdup(key));
        type = (group->type==LWQQ_GROUP_QUN)? QQ_ROOM_TYPE_QUN:QQ_ROOM_TYPE_DISCU;
        g_hash_table_insert(components,g_strdup(QQ_ROOM_TYPE),g_strdup(type));
        chat = purple_chat_new(account,key,components);
        purple_blist_add_chat(chat,lwqq_group_is_qun(group)?qun:talk,NULL);
        //we shouldn't save it.
        if(!group->account) purple_blist_node_set_flags(PURPLE_BLIST_NODE(chat),PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    } else {
        components = chat->components;
        if(!g_hash_table_lookup(components,QQ_ROOM_TYPE)){
            type = (group->type==LWQQ_GROUP_QUN)? QQ_ROOM_TYPE_QUN:QQ_ROOM_TYPE_DISCU;
            g_hash_table_insert(components,s_strdup(QQ_ROOM_TYPE),g_strdup(type));
        }

        if(!group->account) purple_blist_node_set_flags(PURPLE_BLIST_NODE(chat),PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    }

    qq_chat_group* cg = qq_cgroup_new(QQ_CG_OPT);
    group->data = cg;
    cg->group = group;
    cg->chat = chat;
    
    if(lwqq_group_is_qun(group)){
        qq_set_group_name(cg);
        if(purple_buddy_icons_node_has_custom_icon(PURPLE_BLIST_NODE(chat))==0){
            LwqqAsyncEvent* ev = lwqq_info_get_group_avatar(lc,group);
            lwqq_async_add_event_listener(ev,_C_(2p,group_avatar,ev,group));
        }
    }else{
        qq_set_group_name(cg);
    }

    qq_account_insert_index_node(ac, NULL, group);

    ac->disable_send_server = 0;
}
#define discu_come(lc,data) (group_come(lc,data))
static void buddy_message(LwqqClient* lc,LwqqMsgMessage* msg)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* pc = ac->gc;
    char buf[BUFLEN]={0};
    //clean buffer
    strcpy(buf,"");
    LwqqBuddy* buddy = msg->buddy.from;
    const char* local_id = (ac->flag&QQ_USE_QQNUM)?buddy->qqnumber:buddy->uin;

    translate_struct_to_message(ac,msg,buf);
    serv_got_im(pc, local_id, buf, PURPLE_MESSAGE_RECV, msg->time);
}
static void offline_file(LwqqClient* lc,LwqqMsgOffFile* msg)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* pc = ac->gc;
    char buf[4096]={0};
    snprintf(buf,sizeof(buf),_("You Got a Offline File: %s\n"
             "End timeline: %s"
             "<a href=\"%s\">Click to download</a>"),
             msg->name,ctime(&msg->expire_time),lwqq_msg_offfile_get_url(msg));
    serv_got_im(pc,serv_id_to_local(ac,msg->super.from),buf,PURPLE_MESSAGE_RECV,time(NULL));
}
static void notify_offfile(LwqqClient* lc,LwqqMsgNotifyOfffile* notify)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* pc = ac->gc;
    char buf[512]={0};
    const char* action = (notify->action==NOTIFY_OFFFILE_REFUSE)?_("Refuse"):_("Agree");
    snprintf(buf,sizeof(buf),_("Peer %s Accept receive Offline file(%s)\n"),action,notify->filename);
    serv_got_im(pc,serv_id_to_local(ac,notify->super.from),buf,PURPLE_MESSAGE_RECV|PURPLE_MESSAGE_SYSTEM,time(NULL));
}
static void input_notify(LwqqClient* lc,LwqqMsgInputNotify* input)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* pc = ac->gc;
    serv_got_typing(pc,serv_id_to_local(ac,input->from),5,PURPLE_TYPING);
}
static void shake_message(LwqqClient* lc,LwqqMsgShakeMessage* shake)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* pc = ac->gc;
    serv_got_attention(pc, serv_id_to_local(ac, shake->super.from), 0);
}
static void format_body_from_buddy(char* body,size_t buf_len,LwqqBuddy* buddy)
{
    char body_[1024] = {0};
#define ADD_INFO(k,v)  if(v) format_append(body_,"%s:%s\n",k,v)
    ADD_INFO(_("QQ"), buddy->qqnumber);
    ADD_INFO(_("Nick"), buddy->nick);
    ADD_INFO(_("Longnick"), buddy->personal);
    ADD_INFO(_("Gender"), qq_gender_to_str(buddy->gender));
    ADD_INFO(_("Shengxiao"), qq_shengxiao_to_str(buddy->shengxiao));
    ADD_INFO(_("Constellation"), qq_constel_to_str(buddy->constel));
    ADD_INFO(_("Blood Type"), qq_blood_to_str(buddy->blood));
    ADD_INFO(_("Birthday"),lwqq_date_to_str(buddy->birthday));
    ADD_INFO(_("Country"), buddy->country);
    ADD_INFO(_("Province"), buddy->province);
    ADD_INFO(_("City"), buddy->city);
    ADD_INFO(_("Phone"), buddy->phone);
    ADD_INFO(_("Mobile"), buddy->mobile);
    ADD_INFO(_("Email"), buddy->email);
    ADD_INFO(_("Occupation"), buddy->occupation);
    ADD_INFO(_("College"), buddy->college);
    ADD_INFO(_("Homepage"), buddy->homepage);
    //ADD_INFO("说明", buddy->);
#undef ADD_INFO
    strncat(body,body_,buf_len-strlen(body));
}
static void request_join_confirm(LwqqClient* lc,LwqqMsgSysGMsg* msg,LwqqConfirmTable* ct)
{
    if(ct->answer != LWQQ_IGNORE)
        lwqq_info_answer_request_join_group(lc, msg,ct->answer, ct->input);
    lwqq_ct_free(ct);
    lwqq_msg_free((LwqqMsg*)msg);
}
static void sys_g_request_join(LwqqClient* lc,LwqqBuddy* buddy,LwqqMsgSysGMsg* msg)
{
    char body[1024]={0};
    LwqqGroup* g = find_group_by_gid(lc, msg->group_uin);
    if(!g) return ;
    format_append(body,_("Request QQ Group:%s\nRequest Reason:%s\n"),g->name,msg->msg);
    format_body_from_buddy(body,sizeof(body),buddy);
    LwqqConfirmTable* ct = s_malloc0(sizeof(*ct));
    ct->title = s_strdup(_("QQ Group Request Confirm"));
    ct->body = s_strdup(body);
    ct->flags = LWQQ_CT_ENABLE_IGNORE;
    ct->input_label = s_strdup(_("Deny Reason"));
    ct->cmd = _C_(3p,request_join_confirm,lc,msg,ct);
    show_confirm_table(lc,ct);
    lwqq_buddy_free(buddy);
}
static void sys_g_message(LwqqClient* lc,LwqqMsgSysGMsg* msg)
{
    qq_account* ac = lc->data;
    char body[1024]={0};
    switch(msg->type){
        case GROUP_CREATE:
            purple_notify_message(ac->gc, PURPLE_NOTIFY_MSG_INFO, _("QQ Group Sys Message"), _("You create a new group"), NULL, NULL, NULL);
            break;
        case GROUP_JOIN:
        case GROUP_REQUEST_JOIN_AGREE:
            {
                if(!msg->group)
                    return;
                snprintf(body,sizeof(body),_("%s Added in QQ Group[%s]\nAdmin:%s"),
                        msg->is_myself?_("You"):msg->member,
                        msg->group->name,
                        msg->admin);
                if(msg->is_myself)
                    group_come(lc, msg->group);
            }
            break;
        case GROUP_LEAVE:
            {
                if(!msg->group)
                    return;
                snprintf(body,sizeof(body),_("%s Leave QQ Group[%s]"),
                        msg->is_myself?_("You"):msg->member,
                        msg->group->name);
                PurpleChat* chat = purple_blist_find_chat(ac->account, try_get(msg->group->account,msg->group->gid));
                if(chat&&msg->is_myself){
                    purple_blist_remove_chat(chat);
                    //purple_chat_destroy(chat);
                }
            }
            break;
        case GROUP_REQUEST_JOIN_DENY:
            snprintf(body,sizeof(body),_("QQ Group [%s] refused your request"),msg->account);
            purple_notify_message(ac->gc, PURPLE_NOTIFY_MSG_INFO, _("QQ Group Sys Message"),body, NULL, NULL, NULL);
            break;
        case GROUP_REQUEST_JOIN:
            {
                LwqqBuddy* buddy = lwqq_buddy_new();
                LwqqMsgSysGMsg* nmsg = s_malloc0(sizeof(*nmsg));
                lwqq_msg_move(nmsg,msg);
                LwqqAsyncEvent* ev = lwqq_info_get_stranger_info_by_msg(lc,nmsg,buddy);
                lwqq_async_add_event_listener(ev, _C_(3p,sys_g_request_join,lc,buddy,nmsg));
                return;
            } break;
        default:
            return;
            break;
    }
    purple_notify_message(ac->gc, PURPLE_NOTIFY_MSG_INFO, ("QQ Group Sys Message"), body,NULL, NULL, NULL);
}
struct rewrite_pic_entry {
    LwqqGroup* owner;
    int ori_id;
    int new_id;
};
static void rewrite_whole_message_list(LwqqAsyncEvent* ev,qq_account* ac,LwqqGroup* group)
{
    if(lwqq_async_event_get_code(ev)==LWQQ_CALLBACK_FAILED) return;
    group_member_list_come(ac,group);

    PurpleConnection* pc = ac->gc;
    PurpleConversation* conv = purple_find_chat(pc, opend_chat_search(ac,group));
    if(conv == NULL){
        //only do free work.
        GList* item = ac->rewrite_pic_list,*safe;
        struct rewrite_pic_entry* entry;
        while(item){
            entry = item->data;
            safe = item;
            item = item->next;
            if(entry->owner == group){
                purple_imgstore_unref_by_id(entry->new_id);
                ac->rewrite_pic_list = g_list_remove_link(ac->rewrite_pic_list,safe);
            }
        }
        return;
    }
    GList* list = purple_conversation_get_message_history(conv);
    GList* newlist = NULL;
    PurpleConvMessage* message,* newmsg;
    const char* pic;
    char buf[6]={0};
    while(list){
        message = list->data;
        newmsg = s_malloc0(sizeof(*newmsg));
        newmsg->what = s_strdup(message->what);
        pic = newmsg->what;
        if(strstr(pic,"<IMG")==NULL){
        }else{
            char* beg;
            //beg = message->what;
            //inplace replace id num with new id;
            while((pic = strstr(pic,"<IMG")) != NULL){
                int id,new_id = 0;
                sscanf(pic,"<IMG ID=\"%d\"",&id);
                GList* item = ac->rewrite_pic_list;
                struct rewrite_pic_entry* entry;
                while(item){
                    entry = item->data;
                    if(entry->ori_id == id){
                        new_id = entry->new_id;
                        s_free(entry);
                        ac->rewrite_pic_list = g_list_remove_link(ac->rewrite_pic_list,item);
                        break;
                    }
                    item = item->next;
                }
                if(new_id == 0){
                    pic++;
                    continue;
                }
                beg = strchr(pic,'"')+1;
                snprintf(buf,sizeof(buf),"%4d",new_id);
                memcpy(beg,buf,4);
                pic++;
            }
        }
        newmsg->who = s_strdup(message->who);
        newmsg->when = message->when;
        //pic = newmsg->what;
        newlist = g_list_prepend(newlist,newmsg);
        list = list->next;
    }
    purple_conversation_clear_message_history(conv);
    list = newlist;
    while(list){
        message = list->data;
        //pic = message->what;
        //group_message_delay_display(ac, group, message->who, message->what, message->when);
        qq_cgroup_got_msg(group->data, message->who, PURPLE_MESSAGE_RECV, message->what, message->when);
        s_free(message->what);
        s_free(message->who);
        list = list->next;
    }
    g_list_free(newlist);
}


static int group_message(LwqqClient* lc,LwqqMsgMessage* msg)
{
    qq_account* ac = lwqq_client_userdata(lc);
    LwqqGroup* group = find_group_by_gid(lc,(msg->super.super.type==LWQQ_MS_DISCU_MSG)?msg->discu.did:msg->super.from);

    if(group == NULL) return FAILED;

    //force open dialog
    //qq_conv_open(pc,group);
    static char buf[BUFLEN] ;
    strcpy(buf,"");

    translate_struct_to_message(ac,msg,buf);

    //get all member list
    /*LwqqCommand cmd = _C_(4pl,group_message_delay_display,ac,group,s_strdup(msg->group.send),s_strdup(buf),msg->time);
    if(LIST_EMPTY(&group->members)) {
        //use block method to get list
        LwqqAsyncEvent* ev = lwqq_info_get_group_detail_info(lc,group,NULL);
        lwqq_async_add_event_listener(ev,cmd);
    } else {
        vp_do(cmd,NULL);
    }*/
    if(LIST_EMPTY(&group->members)) {
        const char* pic = buf;
        while((pic = strstr(pic,"<IMG"))!=NULL){
            int id;
            sscanf(pic, "<IMG ID=\"%d\">",&id);
            PurpleStoredImage* img = purple_imgstore_find_by_id(id);
            size_t len = purple_imgstore_get_size(img);
            void * img_data = s_malloc(len);
            memcpy(img_data,purple_imgstore_get_data(img),len);
            int new_id = purple_imgstore_add_with_id(img_data, len, NULL);
            struct rewrite_pic_entry* entry = s_malloc0(sizeof(*entry));
            entry->ori_id = id;
            entry->new_id = new_id;
            entry->owner = group;
            ac->rewrite_pic_list = g_list_append(ac->rewrite_pic_list,entry);
            pic++;
        }
        //first check there is a event on queue.
        //if it is. it would do anything.
        //so we didn't do in this.
        LwqqAsyncEvent* ev = lwqq_async_queue_find(&group->ev_queue,lwqq_info_get_group_detail_info);
        if(ev == NULL){
            ev = lwqq_info_get_group_detail_info(lc,group,NULL);
            lwqq_async_add_event_listener(ev,_C_(3p,rewrite_whole_message_list,ev,ac,group));
        }
    }//else set user list in cgroup_got_msg

    qq_cgroup_got_msg(group->data, msg->group.send, PURPLE_MESSAGE_RECV, buf, msg->time);
    return SUCCESS;
}
/*static int group_message_delay_display(qq_account* ac,LwqqGroup* group,const char* sender,const char* buf,time_t t)
{
    PurpleConnection* pc = ac->gc;
    const char* who;
    LwqqBuddy* buddy;

    if((buddy = ac->qq->find_buddy_by_uin(ac->qq,sender))!=NULL) {
        who = (ac->qq_use_qqnum&&buddy->qqnumber)?buddy->qqnumber:sender;
    } else {
        LwqqSimpleBuddy* sb = lwqq_group_find_group_member_by_uin(group,sender);
        if(sb)
            who = sb->card?sb->card:sb->nick;
        else
            who = sender;
    }

    //group_member_list_come(ac,group);
    //serv_got_chat_in(pc,opend_chat_search(ac,group),who,PURPLE_MESSAGE_RECV,buf,t);
    qq_cgroup_got_msg(group->data, who, PURPLE_MESSAGE_RECV, buf, t);
    //s_free(sender);
    //s_free(buf);
    return 0;
}*/
static void whisper_message(LwqqClient* lc,LwqqMsgMessage* mmsg)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* pc = ac->gc;

    const char* from = mmsg->super.from;
    const char* gid = mmsg->sess.id;
    char name[70]={0};
    static char buf[BUFLEN]={0};
    strcpy(buf,"");

    translate_struct_to_message(ac,mmsg,buf);

    LwqqGroup* group = find_group_by_gid(lc,gid);
    if(group == NULL) {
        snprintf(name,sizeof(name),"%s #(broken)# %s",from,gid);
        serv_got_im(pc,name,buf,PURPLE_MESSAGE_RECV,mmsg->time);
        return;
    }
    void** data = s_malloc0(sizeof(void*)*5);
    data[0] = pc;
    data[1] = group;
    data[2] = s_strdup(from);
    data[3] = s_strdup(buf);
    data[4] = (void*)mmsg->time;
    LwqqCommand cmd = _C_(4pl,whisper_message_delay_display,ac,group,s_strdup(from),s_strdup(buf),mmsg->time);
    if(LIST_EMPTY(&group->members)) {
        lwqq_async_add_event_listener(lwqq_info_get_group_detail_info(lc,group,NULL),cmd);
    } else
        vp_do(cmd,NULL);
}
static void whisper_message_delay_display(qq_account* ac,LwqqGroup* group,char* from,char* msg,time_t t)
{
    PurpleConnection* pc = purple_account_get_connection(ac->account);
    char name[70]={0};
    LwqqSimpleBuddy* sb = lwqq_group_find_group_member_by_uin(group,from);
    if(sb == NULL) {
        snprintf(name,sizeof(name),"%s #(broken)# %s",from,group->name);
    } else {
        snprintf(name,sizeof(name),"%s ### %s",(sb->card)?sb->card:sb->nick,group->name);
    }
    serv_got_im(pc,name,msg,PURPLE_MESSAGE_RECV,t);
}
static void status_change(LwqqClient* lc,LwqqMsgStatusChange* status)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleAccount* account = ac->account;
    const char* who;
    if(ac->flag&QQ_USE_QQNUM){
        LwqqBuddy* buddy = find_buddy_by_uin(lc, status->who);
        if(buddy==NULL || buddy->qqnumber == NULL) return;
        who = buddy->qqnumber;
    }else{
        who = status->who;
    }
    purple_prpl_got_user_status(account,who,
            status->client_type==LWQQ_CLIENT_MOBILE?"mobile":status->status,NULL);
}
static void kick_message(LwqqClient* lc,LwqqMsgKickMessage* kick)
{
    qq_account* ac = lwqq_client_userdata(lc);
    char* reason;
    if(kick->show_reason) reason = kick->reason;
    else reason = _("Oups, You was kicked offline by somewhat");
    purple_connection_error_reason(ac->gc,PURPLE_CONNECTION_ERROR_OTHER_ERROR,reason);
}
static void verify_required_confirm(LwqqClient* lc,char* account,LwqqConfirmTable* ct)
{
    if(ct->answer == LWQQ_NO)
        lwqq_info_answer_request_friend(lc, account, ct->answer, ct->input);
    else if(ct->answer == LWQQ_IGNORE){
        //ignore it.
    }else
        lwqq_info_answer_request_friend(lc, account, ct->answer, NULL);
    lwqq_ct_free(ct);
    s_free(account);
}
static void system_message(LwqqClient* lc,LwqqMsgSystem* system,LwqqBuddy* buddy)
{
    qq_account* ac = lwqq_client_userdata(lc);
    char buf1[256]={0};
    if(system->type == VERIFY_REQUIRED) {
        char buf2[2048];
        LwqqConfirmTable* ct = s_malloc0(sizeof(*ct));
        ct->title = s_strdup(_("Friend Confirm"));
        snprintf(buf2,sizeof(buf2),
                _("%s\nRequest as your friend\nAdditional Reason:%s\n\n"),system->account,system->verify_required.msg);
        format_body_from_buddy(buf2,sizeof(buf2),buddy);
        ct->body = s_strdup(buf2);
        ct->exans_label = s_strdup(_("Agree and add back"));
        ct->input_label = s_strdup(_("Refuse reason"));
        ct->flags = LWQQ_CT_ENABLE_IGNORE;
        ct->cmd = _C_(3p,verify_required_confirm,lc,s_strdup(system->account),ct);
        show_confirm_table(lc, ct);
        lwqq_buddy_free(buddy);
        lwqq_msg_free((LwqqMsg*)system);
    } else if(system->type == VERIFY_PASS_ADD) {
        snprintf(buf1,sizeof(buf1),_("%s accept your request,and add back you as friend too"),system->account);
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,_("System Message"),_("Add Friend"),buf1,NULL,NULL);
    } else if(system->type == VERIFY_PASS) {
        snprintf(buf1,sizeof(buf1),_("%s accept your request"),system->account);
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,_("System Message"),_("Add Friend"),buf1,NULL,NULL);
    }
}

static void write_buddy_to_db(LwqqClient* lc,LwqqBuddy* b)
{
    qq_account* ac = lwqq_client_userdata(lc);

    lwdb_userdb_insert_buddy_info(ac->db, b);
    friend_come(lc, b);
}
static void blist_change(LwqqClient* lc,LwqqMsgBlistChange* blist)
{
    LwqqSimpleBuddy* sb;
    LwqqBuddy* buddy;
    LwqqAsyncEvent* ev;
    LwqqAsyncEvset* set;
    LIST_FOREACH(sb,&blist->added_friends,entries){
        //in this. we didn't add it to fast cache.
        buddy = lwqq_buddy_find_buddy_by_uin(lc, sb->uin);
        if(!buddy->qqnumber){
            set = lwqq_async_evset_new();
            ev = lwqq_info_get_friend_qqnumber(lc,buddy);
            lwqq_async_evset_add_event(set, ev);
            ev = lwqq_info_get_friend_detail_info(lc, buddy);
            lwqq_async_evset_add_event(set, ev);
            lwqq_async_add_evset_listener(set,_C_(2p,write_buddy_to_db,lc,buddy));
        }else{
            friend_come(lc, buddy);
        }

    }
}
static void friend_avatar(qq_account* ac,LwqqBuddy* buddy)
{
    PurpleAccount* account = ac->account;
    if(buddy->avatar_len==0)return ;

    const char* key = try_get(buddy->qqnumber,buddy->uin);
    if(strcmp(buddy->uin,purple_account_get_username(account))==0) {
        purple_buddy_icons_set_account_icon(account,(guchar*)buddy->avatar,buddy->avatar_len);
    } else {
        purple_buddy_icons_set_for_user(account,key,(guchar*)buddy->avatar,buddy->avatar_len,NULL);
    }
    buddy->avatar = NULL;
    buddy->avatar_len = 0;
}
static void group_avatar(LwqqAsyncEvent* ev,LwqqGroup* group)
{
    qq_account* ac = lwqq_async_event_get_owner(ev)->data;
    
    PurpleAccount* account = ac->account;
    PurpleChat* chat;
    if(group->avatar_len==0)return ;

    chat = purple_blist_find_chat(account,try_get(group->account,group->gid));
    if(chat==NULL) return ;
    purple_buddy_icons_node_set_custom_icon(PURPLE_BLIST_NODE(chat),(guchar*)group->avatar,group->avatar_len);
    //let free by purple;
    group->avatar = NULL;
    return ;
}
static void lost_connection(LwqqClient* lc)
{
    if(!lwqq_client_valid(lc)) return ;
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* gc = ac->gc;
    purple_connection_error_reason(gc,PURPLE_CONNECTION_ERROR_NETWORK_ERROR,_("webqq lost connection,relogin now,please retry by hand later"));
}
void qq_msg_check(LwqqClient* lc)
{
    if(!lwqq_client_valid(lc)) return;
    LwqqRecvMsgList* l = lc->msg_list;
    LwqqRecvMsg *msg,*prev;
    LwqqMsgSystem* sys_msg;

    pthread_mutex_lock(&l->mutex);
    if (TAILQ_EMPTY(&l->head)) {
        /* No message now, wait 100ms */
        pthread_mutex_unlock(&l->mutex);
        return ;
    }
    msg = TAILQ_FIRST(&l->head);
    while(msg) {
        int res = SUCCESS;
        if(msg->msg) {
            switch(lwqq_mt_bits(msg->msg->type)) {
            case LWQQ_MT_MESSAGE:
                switch(msg->msg->type){
                    case LWQQ_MS_BUDDY_MSG:
                        buddy_message(lc,(LwqqMsgMessage*)msg->msg);
                        break;
                    case LWQQ_MS_GROUP_MSG:
                    case LWQQ_MS_DISCU_MSG:
                    case LWQQ_MS_GROUP_WEB_MSG:
                        res = group_message(lc,(LwqqMsgMessage*)msg->msg);
                        break;
                    case LWQQ_MS_SESS_MSG:
                        whisper_message(lc,(LwqqMsgMessage*)msg->msg);
                        break;
                    default: break;
                }
                break;
            case LWQQ_MT_STATUS_CHANGE:
                status_change(lc,(LwqqMsgStatusChange*)msg->msg);
                break;
            case LWQQ_MT_KICK_MESSAGE:
                kick_message(lc,(LwqqMsgKickMessage*)msg->msg);
                break;
            case LWQQ_MT_SYSTEM:
                sys_msg = (LwqqMsgSystem*)msg->msg;
                if(sys_msg->type == VERIFY_REQUIRED){
                    msg->msg = NULL;
                    LwqqBuddy* buddy = lwqq_buddy_new();
                    LwqqAsyncEvent* ev = lwqq_info_get_stranger_info(
                            lc, sys_msg->super.from, buddy);
                    lwqq_async_add_event_listener(ev, 
                            _C_(3p,system_message,lc,sys_msg,buddy));
                }else{
                    system_message(lc,(LwqqMsgSystem*)msg->msg,NULL);
                }
                break;
            case LWQQ_MT_BLIST_CHANGE:
                blist_change(lc,(LwqqMsgBlistChange*)msg->msg);
                break;
            case LWQQ_MT_OFFFILE:
                offline_file(lc,(LwqqMsgOffFile*)msg->msg);
                break;
            case LWQQ_MT_FILE_MSG:
                file_message(lc,(LwqqMsgFileMessage*)msg->msg);
                break;
            case LWQQ_MT_FILETRANS:
                //complete_file_trans(lc,(LwqqMsgFileTrans*)msg->msg->opaque);
                break;
            case LWQQ_MT_NOTIFY_OFFFILE:
                notify_offfile(lc,(LwqqMsgNotifyOfffile*)msg->msg);
                break;
            case LWQQ_MT_INPUT_NOTIFY:
                input_notify(lc,(LwqqMsgInputNotify*)msg->msg);
                break;
            case LWQQ_MT_SYS_G_MSG:
                sys_g_message(lc,(LwqqMsgSysGMsg*)msg->msg);
                break;
            case LWQQ_MT_SHAKE_MESSAGE:
                shake_message(lc,(LwqqMsgShakeMessage*)msg->msg);
                break;
            default:
                lwqq_puts("unknow message\n");
                break;
            }
        }

        prev = msg;
        msg = TAILQ_NEXT(msg,entries);
        if(res == SUCCESS){
            TAILQ_REMOVE(&l->head,prev, entries);
            lwqq_msg_free(prev->msg);
            s_free(prev);
        }
    }
    pthread_mutex_unlock(&l->mutex);
    return ;

}


/*static void check_exist(void* data,void* userdata)
{
    qq_account* ac = userdata;
    PurpleBuddy* bu = data;
    LwqqClient* lc = ac->qq;
    if(purple_buddy_get_account(bu)==ac->account&&
            find_buddy_by_qqnumber(lc,purple_buddy_get_name(bu))==NULL){
        purple_blist_remove_buddy(bu);
    }
}*/

static void login_stage_f(LwqqClient* lc)
{
    LwqqBuddy* buddy;
    LwqqGroup* group;
    qq_account* ac = lc->data;
    lwdb_userdb_begin(ac->db);
    LIST_FOREACH(buddy,&lc->friends,entries) {
        if(buddy->last_modify == -1 || buddy->last_modify == 0){
            lwdb_userdb_insert_buddy_info(ac->db, buddy);
            friend_come(lc, buddy);
        }
    }
    LIST_FOREACH(group,&lc->groups,entries){
        if(group->last_modify == -1 || group->last_modify == 0){
            lwdb_userdb_insert_group_info(ac->db, group);
            group_come(lc,group);
        }
    }
    lwdb_userdb_commit(ac->db);
}

static void login_stage_3(LwqqClient* lc)
{
    qq_account* ac = lwqq_client_userdata(lc);

    purple_connection_set_state(purple_account_get_connection(ac->account),PURPLE_CONNECTED);

    if(!purple_account_get_alias(ac->account))
        purple_account_set_alias(ac->account,lc->myself->nick);
    if(purple_buddy_icons_find_account_icon(ac->account)==NULL){
        LwqqAsyncEvent* ev=lwqq_info_get_friend_avatar(lc,lc->myself);
        lwqq_async_add_event_listener(ev,_C_(2p,friend_avatar,ac,lc->myself));
    }

    LwqqAsyncEvent* ev = NULL;

    lwdb_userdb_flush_buddies(ac->db, 5,5);
    lwdb_userdb_flush_groups(ac->db, 1,10);

    if(ac->flag&QQ_USE_QQNUM){
        //lwdb_userdb_write_to_client(ac->db, lc);
        lwdb_userdb_query_qqnumbers(lc, ac->db);
        //lwdb_userdb_begin(ac->db,"insertion");
    }

    //we must put buddy and group clean before any add operation.
    GSList* ptr = purple_blist_get_buddies();
    while(ptr){
        PurpleBuddy* buddy = ptr->data;
        if(buddy->account == ac->account){
            const char* qqnum = purple_buddy_get_name(buddy);
            //if it isn't a qqnumber,we should delete it whatever.
            if(lwqq_buddy_find_buddy_by_qqnumber(lc,qqnum)==NULL){
                purple_blist_remove_buddy(buddy);
            }
        }
        ptr = ptr->next;
    }

    //clean extra duplicated node
    all_reset(ac,RESET_GROUP_SOFT|RESET_DISCU);

    LwqqAsyncEvset* set = lwqq_async_evset_new();
    
    LwqqBuddy* buddy;
    LIST_FOREACH(buddy,&lc->friends,entries) {
        lwdb_userdb_query_buddy(ac->db, buddy);
        if((ac->flag& QQ_USE_QQNUM)&& ! buddy->qqnumber){
            ev = lwqq_info_get_friend_qqnumber(lc,buddy);
            lwqq_async_evset_add_event(set, ev);
        }
        if(buddy->last_modify == 0 || buddy->last_modify == -1) {
            ev = lwqq_info_get_single_long_nick(lc, buddy);
            lwqq_async_evset_add_event(set, ev);
            ev = lwqq_info_get_level(lc, buddy);
            lwqq_async_evset_add_event(set, ev);
            //if buddy is unknow we should update avatar in friend_come
            //for better speed in first load
            if(buddy->last_modify == LWQQ_LAST_MODIFY_RESET){
                ev = lwqq_info_get_friend_avatar(lc,buddy);
                lwqq_async_evset_add_event(set, ev);
            }
        }
        if(buddy->last_modify != -1 && buddy->last_modify != 0)
            friend_come(lc,buddy);
    }
    LwqqGroup* group;
    LIST_FOREACH(group,&lc->groups,entries) {
        //LwqqAsyncEvset* set = NULL;
        lwdb_userdb_query_group(ac->db, group);
        if((ac->flag && QQ_USE_QQNUM)&& ! group->account){
            ev = lwqq_info_get_group_qqnumber(lc,group);
            lwqq_async_evset_add_event(set, ev);
        }
        if(group->last_modify == -1 || group->last_modify == 0){
            ev = lwqq_info_get_group_memo(lc, group);
            lwqq_async_evset_add_event(set, ev);
        }
        //because group avatar less changed.
        //so we dont reload it.
        if(group->last_modify != -1 && group->last_modify != 0)
            group_come(lc,group);
    }
    lwqq_async_add_evset_listener(set, _C_(p,login_stage_f,lc));
    //after this we finished the qqnumber fast index.
    

    //we should always put discu after group deletion.
    //to avoid delete discu list.
    LwqqGroup* discu;
    LIST_FOREACH(discu,&lc->discus,entries) {
        discu_come(lc,discu);
    }

    ac->state = LOAD_COMPLETED;
    LwqqPollOption flags = POLL_AUTO_DOWN_DISCU_PIC|POLL_AUTO_DOWN_GROUP_PIC|POLL_AUTO_DOWN_BUDDY_PIC;
    if(ac->flag& REMOVE_DUPLICATED_MSG)
        flags |= POLL_REMOVE_DUPLICATED_MSG;
    if(ac->flag& NOT_DOWNLOAD_GROUP_PIC)
        flags &= ~POLL_AUTO_DOWN_GROUP_PIC;

    lc->msg_list->poll_msg(lc->msg_list,flags);
}

static void upload_content_fail(LwqqClient* lc,const char* serv_id,LwqqMsgContent* c,int err)
{
    switch(c->type){
        case LWQQ_CONTENT_OFFPIC:
            qq_sys_msg_write(lc->data, LWQQ_MS_BUDDY_MSG, serv_id, _("Send Pic Failed"), PURPLE_MESSAGE_ERROR, time(NULL));
            break;
        case LWQQ_CONTENT_CFACE:
            qq_sys_msg_write(lc->data, LWQQ_MS_GROUP_MSG, serv_id, err==LWQQ_EC_UPLOAD_OVERSIZE?_("Send Pic over 1MB"):_("Send Pic Failed"), PURPLE_MESSAGE_ERROR, time(NULL));
            break;
        default:break;
    }
}

static void input_verify_image(LwqqVerifyCode* code,PurpleRequestFields* fields)
{
    const gchar *value;
    value = purple_request_fields_get_string(fields, "code_entry");
    code->str = s_strdup(value);

    vp_do(code->cmd,NULL);
}

static void cancel_verify_image(LwqqVerifyCode* code,PurpleRequestField* fields)
{
    vp_do(code->cmd,NULL);
}

static void show_verify_image(LwqqClient* lc,LwqqVerifyCode* code)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleRequestFieldGroup *field_group;
    PurpleRequestField *code_entry;
    PurpleRequestField *code_pic;
    PurpleRequestFields *fields;

    fields = purple_request_fields_new();
    field_group = purple_request_field_group_new((gchar*)0);
    purple_request_fields_add_group(fields, field_group);

    code_pic = purple_request_field_image_new("code_pic", "code", code->data, code->size);
    purple_request_field_group_add_field(field_group, code_pic);
    lwqq_util_save_img(code->data, code->size, lc->username,LWQQ_CACHE_DIR);

    code_entry = purple_request_field_string_new("code_entry", "input", "", FALSE);
    purple_request_field_set_required(code_entry,TRUE);
    purple_request_field_group_add_field(field_group, code_entry);

    purple_request_fields(ac->account, NULL,
                          _("Captcha"), NULL,
                          fields, _("OK"), G_CALLBACK(input_verify_image),
                          _("Cancel"), G_CALLBACK(cancel_verify_image),
                          ac->account, NULL, NULL, code);

    return ;
}

static void confirm_table_yes(LwqqConfirmTable* table,PurpleRequestFields* fields)
{
    if(table->exans_label){
        table->answer = purple_request_fields_get_choice(fields,"choice");
    }else 
        table->answer = LWQQ_YES;
    if(table->input_label){
        const char* i = purple_request_fields_get_string(fields, "input");
        table->input = s_strdup(i);
    }
    vp_do(table->cmd,NULL);
}
static void confirm_table_no(LwqqConfirmTable* table,PurpleRequestFields* fields)
{
    table->answer = (table->flags&LWQQ_CT_ENABLE_IGNORE)?LWQQ_IGNORE:LWQQ_NO;
    if(table->input_label){
        const char* i = purple_request_fields_get_string(fields, "input");
        table->input = s_strdup(i);
    }
    vp_do(table->cmd,NULL);
}
static void show_confirm_table(LwqqClient* lc,LwqqConfirmTable* table)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleRequestFields *fields;
    PurpleRequestFieldGroup *field_group;

    fields = purple_request_fields_new();
    field_group = purple_request_field_group_new((gchar*)0);
    purple_request_fields_add_group(fields, field_group);

    if(table->body){
        PurpleRequestField* str = purple_request_field_string_new("body", table->title, table->body, TRUE);
        purple_request_field_string_set_editable(str, FALSE);
        purple_request_field_group_add_field(field_group, str);
    }

    if(table->exans_label||table->flags&LWQQ_CT_ENABLE_IGNORE||table->flags&LWQQ_CT_CHOICE_MODE){
        PurpleRequestField* choice = purple_request_field_choice_new("choice", _("Please Select"), table->answer);
        purple_request_field_choice_add(choice,table->no_label?:_("Deny"));
        purple_request_field_choice_add(choice,table->yes_label?:_("Accept"));
        if(table->exans_label)
            purple_request_field_choice_add(choice,table->exans_label);
        purple_request_field_group_add_field(field_group,choice);
    }

    if(table->input_label){
        PurpleRequestField* i = purple_request_field_string_new("input", table->input_label, table->input?:"", FALSE);
        s_free(table->input);
        purple_request_field_group_add_field(field_group,i);
    }

    purple_request_fields(ac->gc, NULL,
                          _("Confirm"), NULL,
                          fields, _("Confirm"), G_CALLBACK(confirm_table_yes),
                          table->flags&LWQQ_CT_ENABLE_IGNORE?_("Ignore"):_("Deny"), G_CALLBACK(confirm_table_no),
                          ac->account, NULL, NULL, table);
}
static void delete_group_local(LwqqClient* lc,const LwqqGroup* g)
{
    qq_chat_group* cg = g->data;
    if(!cg) return;
    qq_account* ac = lc->data;
    qq_account_remove_index_node(ac, NULL, g);
    purple_blist_remove_chat(cg->chat);
}
static void flush_group_members(LwqqClient* lc,LwqqGroup* g)
{
    qq_chat_group* cg = g->data;
    qq_cgroup_flush_members(cg);
}
static LwqqAsyncOption qq_async_opt = {
    .login_complete = login_stage_1,
    .new_friend = friend_come,
    .new_group = group_come,
    .poll_msg = qq_msg_check,
    .poll_lost = lost_connection,
    .upload_fail = upload_content_fail,
    .need_verify2 = show_verify_image,
    .delete_group = delete_group_local,
    .group_members_chg = flush_group_members,
};
static void login_stage_1(LwqqClient* lc,LwqqErrorCode err)
{
    qq_account* ac = lwqq_client_userdata(lc);
    PurpleConnection* gc = purple_account_get_connection(ac->account);
    switch(err){
        case LWQQ_EC_OK:
            break;
        case LWQQ_EC_LOGIN_ABNORMAL:
            purple_connection_error_reason(gc,PURPLE_CONNECTION_ERROR_OTHER_ERROR,_("Account Problem Occurs,Need lift the ban"));
            return ;
        case LWQQ_EC_NETWORK_ERROR:
            purple_connection_error_reason(gc,PURPLE_CONNECTION_ERROR_OTHER_ERROR,_("Network Error"));
            return;
        default:
            purple_connection_error_reason(gc,PURPLE_CONNECTION_ERROR_NETWORK_ERROR,lc->last_err);
            return;
    }

    ac->state = CONNECTED;

    gc->flags |= PURPLE_CONNECTION_HTML;

    LwqqAsyncEvset* set = lwqq_async_evset_new();
    LwqqAsyncEvent* ev;
    ev = lwqq_info_get_friends_info(lc,NULL);
    lwqq_async_evset_add_event(set,ev);
    ev = lwqq_info_get_group_name_list(lc,NULL);
    lwqq_async_evset_add_event(set,ev);
    //ev = lwqq_info_get_single_long_nick(lc, lc->myself);
    //lwqq_async_evset_add_event(set,ev);
    //ev = lwqq_info_recent_list(lc, &ac->recent_list);
    //lwqq_async_evset_add_event(set, ev);
    lwqq_async_add_evset_listener(set,_C_(2p,login_stage_2,set,lc));

    return ;
}
static void login_stage_2(LwqqAsyncEvset* evset,LwqqClient* lc)
{
    int errnum = lwqq_async_evset_get_result(evset);
    if(errnum > 0){
        qq_account* ac = lc->data;
        purple_connection_error_reason(ac->gc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Get Friend|Group List Failed"));
        return;
    }
    LwqqAsyncEvset* set = lwqq_async_evset_new();
    LwqqAsyncEvent* ev;
    ev = lwqq_info_get_discu_name_list(lc);
    lwqq_async_evset_add_event(set,ev);
    ev = lwqq_info_get_online_buddies(lc,NULL);
    lwqq_async_evset_add_event(set,ev);
    
    qq_account* ac = lwqq_client_userdata(lc);
    const char* alias = purple_account_get_alias(ac->account);
    if(alias == NULL){
        ev = lwqq_info_get_friend_detail_info(lc,lc->myself);
        lwqq_async_evset_add_event(set,ev);
    }

    lwqq_async_add_evset_listener(set,_C_(p,login_stage_3,lc));
}

//send back receipt
static void send_receipt(LwqqAsyncEvent* ev,LwqqMsg* msg,char* serv_id,char* what)
{
    if(lwqq_async_event_get_code(ev)==LWQQ_CALLBACK_FAILED) goto failed;
    qq_account* ac = lwqq_async_event_get_owner(ev)->data;
    LwqqMsgMessage* mmsg = (LwqqMsgMessage*)msg;

    if(ev == NULL){
        qq_sys_msg_write(ac,msg->type,serv_id,_("Message body too long"),PURPLE_MESSAGE_ERROR,time(NULL));
    }else{
        int err = lwqq_async_event_get_result(ev);
        static char buf[1024]={0};
        PurpleConversation* conv = find_conversation(msg->type,serv_id,ac);

        if(conv && err > 0){
            snprintf(buf,sizeof(buf),_("Send failed, err:%d:\n%s"),err,what);
            qq_sys_msg_write(ac, msg->type, serv_id, buf, PURPLE_MESSAGE_ERROR, time(NULL));
        }
        if(err == WEBQQ_LOST_CONN){
            ac->qq->async_opt->poll_lost(ac->qq);
        }
    }
    if(mmsg->upload_retry <0)
        qq_sys_msg_write(ac, msg->type, serv_id, _("Upload content retry over limit"), PURPLE_MESSAGE_ERROR, time(NULL));

    if(msg->type == LWQQ_MS_GROUP_MSG) mmsg->group.group_code = NULL;
    else if(msg->type == LWQQ_MS_DISCU_MSG) mmsg->discu.did = NULL;
failed:
    s_free(what);
    s_free(serv_id);
    lwqq_msg_free(msg);
}

//send a message to a friend.
//called by purple
static int qq_send_im(PurpleConnection *gc, const gchar *who, const gchar *what, PurpleMessageFlags UNUSED(flags))
{
    qq_account* ac = (qq_account*)purple_connection_get_protocol_data(gc);
    LwqqClient* lc = ac->qq;
    LwqqMsg* msg;
    LwqqMsgMessage *mmsg;
    LwqqGroup* group = NULL;
    LwqqSimpleBuddy* sb = NULL;
    if(find_group_and_member_by_card(lc, who, &group, &sb)){
        msg = lwqq_msg_new(LWQQ_MS_SESS_MSG);
        mmsg = (LwqqMsgMessage*)msg;

        mmsg->super.to = s_strdup(sb->uin);
        if(!sb->group_sig)
            lwqq_info_get_group_sig(lc,group,sb->uin);
        mmsg->sess.group_sig = s_strdup(sb->group_sig);
        mmsg->sess.service_type = group->type;
    } else {
        msg = lwqq_msg_new(LWQQ_MS_BUDDY_MSG);
        mmsg = (LwqqMsgMessage*)msg;
        if(ac->flag&QQ_USE_QQNUM){
            LwqqBuddy* buddy = find_buddy_by_qqnumber(lc,who);
            if(buddy)
                mmsg->super.to = s_strdup(buddy->uin);
            else mmsg->super.to = s_strdup(who);
        }else{
            mmsg->super.to = s_strdup(who);
        }
    }
    mmsg->f_name = s_strdup(ac->font.family);
    mmsg->f_size = ac->font.size;
    mmsg->f_style = ac->font.style;
    strcpy(mmsg->f_color,"000000");

    translate_message_to_struct(lc, who, what, msg, 0);

    LwqqAsyncEvent* ev = lwqq_msg_send(lc,mmsg);
    lwqq_async_add_event_listener(ev,_C_(4p, send_receipt,ev,msg,strdup(who),strdup(what)));

    return 1;
}

static int qq_send_chat(PurpleConnection *gc, int id, const char *message, PurpleMessageFlags flags)
{
    qq_account* ac = (qq_account*)purple_connection_get_protocol_data(gc);
    LwqqGroup* group = opend_chat_index(ac,id);
    LwqqMsg* msg;

    
    msg = lwqq_msg_new(LWQQ_MS_GROUP_MSG);
    LwqqMsgMessage *mmsg = (LwqqMsgMessage*)msg;
    mmsg->super.to = s_strdup(group->gid);
    if(group->type == LWQQ_GROUP_QUN){
        msg->type = LWQQ_MS_GROUP_MSG;
        mmsg->group.group_code = group->code;
    }else if(group->type == LWQQ_GROUP_DISCU){
        msg->type = LWQQ_MS_DISCU_MSG;
        mmsg->discu.did = group->did;
    }
    mmsg->f_name = s_strdup(ac->font.family);
    mmsg->f_size = ac->font.size;
    mmsg->f_style = ac->font.style;
    strcpy(mmsg->f_color,"000000");
    PurpleConversation* conv = purple_find_chat(gc,id);

    translate_message_to_struct(ac->qq, group->gid, message, msg, 1);

    LwqqAsyncEvent* ev = lwqq_msg_send(ac->qq,mmsg);
    lwqq_async_add_event_listener(ev, _C_(4p,send_receipt,ev,msg,s_strdup(group->gid),s_strdup(message)));
    purple_conversation_write(conv,NULL,message,flags,time(NULL));

    return 1;
}

static unsigned int qq_send_typing(PurpleConnection* gc,const char* local_id,PurpleTypingState state)
{
    if(state != PURPLE_TYPING) return 0;
    //if it is whisper we ignore it.
    if(strstr(local_id," ### ")!=NULL) return 0;
    qq_account* ac = (qq_account*)purple_connection_get_protocol_data(gc);
    lwqq_msg_input_notify(ac->qq,local_id_to_serv(ac,local_id));
    return 0;
}

#if 0
static void qq_leave_chat(PurpleConnection* gc,int id)
{
    printf("leave chat\n");
}

//pidgin not use send_whisper .
//may use it in v 3.0.0
static void qq_send_whisper(PurpleConnection* gc,int id,const char* who,const char* message)
{
    qq_account* ac = (qq_account*)purple_connection_get_protocol_data(gc);
    LwqqClient* lc = ac->qq;
    LwqqGroup* group = opend_chat_index(ac,id);

    LwqqBuddy* buddy = find_buddy_by_uin(lc,who);
    if(buddy!=NULL) {
        qq_send_im(gc,who,message,PURPLE_MESSAGE_WHISPER);
        return;
    }

    LwqqSimpleBuddy* sb = find_group_member_by_nick(group,who);
    if(sb==NULL)
        return;

    LwqqMsg* msg = lwqq_msg_new(LWQQ_MT_SESS_MSG);
    LwqqMsgMessage *mmsg = msg->opaque;
    mmsg->to = sb->uin;
    if(!sb->group_sig)
        lwqq_info_get_group_sig(lc,group,sb->uin);
    mmsg->group_sig = sb->group_sig;
    mmsg->f_name = "宋体";
    mmsg->f_size = 13;
    mmsg->f_style.b = 0,mmsg->f_style.i = 0,mmsg->f_style.u = 0;
    mmsg->f_color = "000000";

    lwqq_msg_send(lc,msg);

}
#endif

GList *qq_chat_info(PurpleConnection *gc)
{
    GList *m;
    struct proto_chat_entry *pce;

    m = NULL;

    pce = g_new0(struct proto_chat_entry, 1);
    pce->label = "QQ";
    pce->identifier = QQ_ROOM_KEY_GID;
    pce->required = TRUE;
    m = g_list_append(m, pce);

    return m;
}
static GHashTable *qq_chat_info_defaults(PurpleConnection *gc, const gchar *chat_name)
{
    GHashTable *defaults;

    defaults = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    if (chat_name != NULL){
        g_hash_table_insert(defaults, g_strdup(QQ_ROOM_KEY_GID), g_strdup(chat_name));
        g_hash_table_insert(defaults, g_strdup(QQ_ROOM_TYPE),    g_strdup(QQ_ROOM_TYPE_QUN));
    }

    return defaults;
}
static PurpleRoomlist* qq_get_all_group_list(PurpleConnection* gc)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    PurpleAccount* account = ac->account;
    PurpleRoomlist* list = purple_roomlist_new(account);

    GList* fields = NULL;
    PurpleRoomlistField* field;
    field = purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING,_("QQ"),QQ_ROOM_KEY_GID,FALSE);
    fields = g_list_append(fields,field);
    field = purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING,_("Type"),QQ_ROOM_TYPE,FALSE);
    fields = g_list_append(fields,field);
    purple_roomlist_set_fields(list,fields);

    LwqqGroup* group;
    LIST_FOREACH(group,&ac->qq->groups,entries) {
        PurpleRoomlistRoom* room = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_ROOM,group->name,NULL);
        purple_roomlist_room_add_field(list,room,group->gid);
        purple_roomlist_room_add_field(list,room,QQ_ROOM_TYPE_QUN);
        purple_roomlist_room_add(list,room);
    }
    LwqqGroup* discu;
    LIST_FOREACH(discu,&ac->qq->discus,entries){
        PurpleRoomlistRoom* room = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_ROOM,discu->name,NULL);
        purple_roomlist_room_add_field(list,room,discu->did);
        purple_roomlist_room_add_field(list,room,QQ_ROOM_TYPE_DISCU);
        purple_roomlist_room_add(list,room);
    }
    return list;
}

static void group_member_list_come(qq_account* ac,LwqqGroup* group)
{
    LwqqClient* lc = ac->qq;
    LwqqSimpleBuddy* member;
    LwqqBuddy* buddy;
    PurpleConvChatBuddyFlags flag;
    GList* users = NULL;
    GList* flags = NULL;
    GList* extra_msgs = NULL;

    PurpleConversation* conv = purple_find_chat(
                                   purple_account_get_connection(ac->account),opend_chat_search(ac,group));
    PurpleConvChat* chat = PURPLE_CONV_CHAT(conv);
    //only there are no member we add it.
    if(purple_conv_chat_get_users(PURPLE_CONV_CHAT(conv))==NULL) {
        LIST_FOREACH(member,&group->members,entries) {
            extra_msgs = g_list_append(extra_msgs,NULL);
            flag = 0;

            if(lwqq_member_is_founder(member,group)) flag |= PURPLE_CBFLAGS_FOUNDER;
            if(member->stat != LWQQ_STATUS_OFFLINE) flag |= PURPLE_CBFLAGS_VOICE;
            if(member->mflag & LWQQ_MEMBER_IS_ADMIN) flag |= PURPLE_CBFLAGS_OP;

            flags = g_list_append(flags,GINT_TO_POINTER(flag));
            if((buddy = find_buddy_by_uin(lc,member->uin))) {
                if(ac->flag&QQ_USE_QQNUM)
                    users = g_list_append(users,try_get(buddy->qqnumber,buddy->uin));
                else
                    users = g_list_append(users,buddy->uin);
            } else {
                users = (member->card)?g_list_append(users,member->card):g_list_append(users,member->nick);
            }
        }
        purple_conv_chat_add_users(chat,users,extra_msgs,flags,FALSE);
        g_list_free(users);
        g_list_free(flags);
        g_list_free(extra_msgs);
    }
    return ;
}
static void qq_group_add_or_join(PurpleConnection *gc, GHashTable *data)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    LwqqClient* lc = ac->qq;
    LwqqGroup* group = NULL;

    char* key = g_hash_table_lookup(data,QQ_ROOM_KEY_GID);
    char* type = g_hash_table_lookup(data,QQ_ROOM_TYPE);
    if(key==NULL) return;
    //if it is new add group so type is NULL
    if(type == NULL){
        //we may delete group .so we need query lc directly.
        group = lwqq_group_find_group_by_qqnumber(lc, key);
        if(group==NULL){
            //from now this is a add group query.
            //we need send to server.
            group = lwqq_group_new(LWQQ_GROUP_QUN);
            LwqqAsyncEvent* ev;
            ev = lwqq_info_search_group_by_qq(ac->qq, key, group);
            lwqq_async_add_event_listener(ev, _C_(2p,search_group_receipt,ev,group));
            return;
        }
    }
    //if above we found there is a group but type is NULL.
    //so we open the group
    if(group==NULL){
        if((ac->flag&QQ_USE_QQNUM)&&!strcmp(type,QQ_ROOM_TYPE_QUN))
            group = lwqq_group_find_group_by_qqnumber(lc,key);
        else
            group = lwqq_group_find_group_by_gid(lc,key);;
        if(group == NULL) return;
    }

    qq_cgroup_open(group->data);
}
static gboolean qq_offline_message(const PurpleBuddy *buddy)
{
    //webqq support offline message indeed
    return TRUE;
}
#if 0
static gboolean qq_can_receive_file(PurpleConnection* gc,const char* who)
{
    //webqq support send recv file indeed.
    return TRUE;
}
#endif
//this return the member of group 's real name
//it is only used when create dialog;
char *qq_get_cb_real_name(PurpleConnection *gc, int id, const char *who)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    char conv_name[70]={0};
    if(purple_find_buddy(ac->account,who)!=NULL)
        return NULL;
    else {
        LwqqGroup* group = opend_chat_index(ac,id);
        LwqqSimpleBuddy* sb = find_group_member_by_nick_or_card(group,who);
        //if(sb==NULL) sb = lwqq_group_find_group_member_by_uin(group, who);
        snprintf(conv_name,sizeof(conv_name),"%s ### %s",(sb->card)?sb->card:sb->nick,group->name);
        return s_strdup(conv_name);
    }
    return NULL;
}

#if 0
static void on_create(void *data,PurpleConnection* gc)
{
    //on conversation create we add smileys to it.
    PurpleConversation* conv = data;
    translate_add_smiley_to_conversation(conv);
}
#endif
static void qq_close(PurpleConnection *gc)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    LwqqErrorCode err;

    if(ac->relink_timer>0) purple_timeout_remove(ac->relink_timer);
    if(lwqq_client_logined(ac->qq))
        lwqq_logout(ac->qq,&err);
    ac->qq->msg_list->poll_close(ac->qq->msg_list);
    LwqqGroup* g;
    LIST_FOREACH(g,&ac->qq->groups,entries){
        qq_cgroup_free((qq_chat_group*)g->data);
    }
    lwqq_client_free(ac->qq);
    lwdb_userdb_free(ac->db);
    qq_account_free(ac);
    purple_connection_set_protocol_data(gc,NULL);
    translate_global_free();
    g_ref_count -- ;
    if(g_ref_count == 0){
        lwqq_http_global_free();
        lwqq_async_global_quit();
        lwdb_global_free();
    }
}
//send change markname to server.
static void qq_change_markname(PurpleConnection* gc,const char* who,const char *alias)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    if(ac->disable_send_server) return;
    LwqqClient* lc = ac->qq;
    LwqqBuddy* buddy = (ac->flag&QQ_USE_QQNUM)?find_buddy_by_qqnumber(lc,who):find_buddy_by_uin(lc,who);
    if(buddy == NULL) return;
    lwqq_info_change_buddy_markname(lc,buddy,alias);
}
void move_buddy_back(void* data)
{
    void** d = data;
    PurpleBuddy* buddy = d[0];
    char* group_name = d[1];
    PurpleGroup* group = purple_find_group(group_name);
    if(group==NULL) group = purple_group_new(group_name);
    s_free(group_name);
    qq_account* ac = d[2];
    s_free(data);
    ac->disable_send_server = 1;
    purple_blist_add_buddy(buddy,NULL,group,NULL);
    ac->disable_send_server = 0;
}
static void change_category_back(LwqqAsyncEvent* event,void* data)
{
    void**d = data;
    qq_account* ac = d[2];
    if(event == NULL) goto clean;
    if(lwqq_async_event_get_result(event)!=0) {
        move_buddy_back(data);
        purple_notify_error(ac->gc,NULL,_("Change friend category failed"),_("Server fault returns"));
        return;
    } 
clean:
    s_free(d[1]);
    s_free(data);
}
static void add_passerby_to_friend_failed(LwqqAsyncEvent* ev,LwqqBuddy* b,void* data)
{
    LwqqClient* lc = ev->lc;
    qq_account* ac = lc->data;
    b->cate_index = LWQQ_FRIEND_CATE_IDX_PASSERBY;
    if(ev->result == 0){
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,NULL,_("Add passerby as friend"),_("Send add request successful"),move_buddy_back,data);
    }else
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_ERROR,NULL,_("Add friend failed"),_("???"),move_buddy_back,data);
}
static void qq_change_category(PurpleConnection* gc,const char* who,const char* old_group,const char* new_group)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    if(ac->disable_send_server) return;
    if(strcmp(new_group,ac->recent_group_name)==0||strcmp(old_group,ac->recent_group_name)==0) return;
    LwqqClient* lc = ac->qq;
    LwqqBuddy* buddy = ac->flag&QQ_USE_QQNUM?find_buddy_by_qqnumber(lc,who):find_buddy_by_uin(lc,who);
    LwqqFriendCategory* cate = NULL;
    if(buddy==NULL) return;

    int cate_index=0;

    void** data = s_malloc0(sizeof(void*)*3);
    data[0] = buddy->data;
    data[1] = s_strdup(old_group);
    data[2] = ac;

    if(strcmp(new_group,QQ_DEFAULT_CATE)==0)
        cate_index = LWQQ_FRIEND_CATE_IDX_DEFAULT;
    else{
        cate = lwqq_category_find_by_name(lc, new_group);
        if(cate ==NULL){
            purple_notify_message(gc,PURPLE_NOTIFY_MSG_ERROR,NULL,_("Change friend category failed"),_("no such category exists"),move_buddy_back,data);
            return;
        }else
            cate_index = cate->index;
    }
    if(buddy->cate_index == LWQQ_FRIEND_CATE_IDX_PASSERBY){
        buddy->cate_index = cate_index;
        LwqqAsyncEvent* ev = lwqq_info_add_friend(lc, buddy, "");
        lwqq_async_add_event_listener(ev, _C_(3p,add_passerby_to_friend_failed,ev,buddy,data));
        return ;
    }
    LwqqAsyncEvent* event;
    event = lwqq_info_modify_buddy_category(lc,buddy,cate_index);

    lwqq_async_add_event_listener(event,_C_(2p,change_category_back,event,data));
}
//keep this empty to ensure rename category dont crash
static void qq_rename_category(PurpleConnection* gc,const char* old_name,PurpleGroup* group,GList* moved_buddies)
{
}
static void qq_remove_buddy(PurpleConnection* gc,PurpleBuddy* buddy,PurpleGroup* group)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    LwqqClient* lc = ac->qq;
    LwqqBuddy* friend = buddy->proto_data;
    if(friend==NULL) return;
    lwqq_info_delete_friend(lc,friend,LWQQ_DEL_FROM_OTHER);
}

static void visit_qqzone(LwqqBuddy* buddy)
{
    char url[256]={0};
    snprintf(url,sizeof(url),"xdg-open 'http://user.qzone.qq.com/%s'",buddy->qqnumber);
    system(url);
}

static void qq_visit_qzone(PurpleBlistNode* node)
{
    PurpleBuddy* buddy = PURPLE_BUDDY(node);
    PurpleAccount* account = purple_buddy_get_account(buddy);
    qq_account* ac = purple_connection_get_protocol_data(
                         purple_account_get_connection(account));
    if(ac->flag&QQ_USE_QQNUM){
        const char* qqnum = purple_buddy_get_name(buddy);
        char url[256]={0};
        snprintf(url,sizeof(url),"xdg-open 'http://user.qzone.qq.com/%s'",qqnum);
        system(url);
        return;
    }else{
        /*
        const char* uin = purple_buddy_get_name(buddy);
        LwqqBuddy* friend = find_buddy_by_uin(ac->qq,uin);
        */
        LwqqBuddy* friend = buddy->proto_data;
        if(friend==NULL) return;
        if(!friend->qqnumber) {
            lwqq_async_add_event_listener(
                    lwqq_info_get_friend_qqnumber(ac->qq,friend),
                    _C_(p,visit_qqzone,friend));
        } else {
            visit_qqzone(friend);
        }
    }
}

static void qq_send_mail(PurpleBlistNode* n)
{
    PurpleBuddy* b = PURPLE_BUDDY(n);
    LwqqBuddy* buddy = b->proto_data;
    char buf[128]={0};
    if(buddy->email) snprintf(buf,sizeof(buf),
            "xdg-open mailto:%s",buddy->email);
    else if(buddy->qqnumber) snprintf(buf,sizeof(buf),
            "xdg-open mailto:%s@qq.com",buddy->qqnumber);
    else return;
    system(buf);
}

static void add_friend_receipt(LwqqAsyncEvent* ev)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    qq_account* ac = lc->data;
    if(err == 6 ){
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,_("Error Message"),_("ErrCode:6\nPlease try agagin later\n"),NULL,NULL,NULL);
    }
}
static void add_friend_ask_message(LwqqClient* lc,LwqqConfirmTable* ask,LwqqBuddy* b)
{
    add_friend(lc,ask,b,s_strdup(ask->input));
}
static void add_friend(LwqqClient* lc,LwqqConfirmTable* c,LwqqBuddy* b,char* message)
{
    if(c->answer == LWQQ_NO){
        goto done;
    }
    if(message==NULL){
        LwqqConfirmTable* ask = s_malloc0(sizeof(*ask));
        ask->input_label = s_strdup(_("Invite Message"));
        ask->cmd = _C_(3p,add_friend_ask_message,lc,ask,b);
        show_confirm_table(lc, ask);
        lwqq_ct_free(c);
        return ;
    }else{
        LwqqAsyncEvent* ev = lwqq_info_add_friend(lc, b,message);
        lwqq_async_add_event_listener(ev, _C_(p,add_friend_receipt,ev));
    }
done:
    lwqq_ct_free(c);
    lwqq_buddy_free(b);
    s_free(message);
}
static void search_buddy_receipt(LwqqAsyncEvent* ev,LwqqBuddy* buddy,char* uni_id,char* message)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    qq_account* ac = lc->data;
    //captcha wrong
    if(err == WEBQQ_FATAL){
        LwqqAsyncEvent* event = lwqq_info_search_friend(lc,uni_id,buddy);
        lwqq_async_add_event_listener(event, _C_(4p,search_buddy_receipt,event,buddy,uni_id,message));
        return;
    }
    if(err == LWQQ_EC_NO_RESULT){
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,_("Error Message"),_("Account not exists or not main display account"),NULL,NULL,NULL);
        goto failed;
    }
    if(!buddy->token){
        purple_notify_message(ac->gc,PURPLE_NOTIFY_MSG_INFO,_("Error Message"),_("Get friend infomation failed"),NULL,NULL,NULL);
        goto failed;
    }
    LwqqConfirmTable* confirm = s_malloc0(sizeof(*confirm));
    confirm->title = s_strdup(_("Friend Confirm"));
    char body[1024] = {0};
    format_body_from_buddy(body,sizeof(body),buddy);
    confirm->body = s_strdup(body);
    confirm->cmd = _C_(4p,add_friend,lc,confirm,buddy,message);
    show_confirm_table(lc,confirm);
    s_free(uni_id);
    return;
failed:
    lwqq_buddy_free(buddy);
    s_free(message);
    s_free(uni_id);
}
static void delete_back_broken_buddy(PurpleConnection* pc,char* name)
{
    qq_account* ac = pc->proto_data;
    PurpleBuddy* buddy = purple_find_buddy(ac->account, name);
    if(buddy){
        purple_blist_remove_buddy(buddy);
    }
}
static void qq_add_buddy(PurpleConnection* pc,PurpleBuddy* buddy,PurpleGroup* group
#if PURPLE_OUTDATE
        )
#else
        ,const char* message)
#endif
{
    qq_account* ac = purple_connection_get_protocol_data(pc);
    LwqqClient* lc = ac->qq;
    const char* uni_id = purple_buddy_get_name(buddy);
    LwqqGroup* g = NULL;
    LwqqSimpleBuddy* sb = NULL;
    LwqqBuddy* friend = lwqq_buddy_new();
    LwqqFriendCategory* cate = lwqq_category_find_by_name(ac->qq,group->name);
    if(cate == NULL){
        friend->cate_index = 0;
    }else
        friend->cate_index = cate->index;

    if(find_group_and_member_by_card(lc, uni_id, &g, &sb)){
        LwqqAsyncEvset* set = lwqq_async_evset_new();
        LwqqAsyncEvent* ev;
        friend->uin = s_strdup(sb->uin);
        ev = lwqq_info_get_group_member_detail(lc, sb->uin, friend);
        lwqq_async_evset_add_event(set, ev);
        ev = lwqq_info_get_friend_qqnumber(lc,friend);
        lwqq_async_evset_add_event(set,ev);
        lwqq_async_add_evset_listener(set, _C_(3p,lwqq_info_add_group_member_as_friend,lc,friend,NULL));
        lc->dispatch(vp_func_2p,(CALLBACK_FUNC)delete_back_broken_buddy,pc,s_strdup(buddy->name));
    }else{
        //friend->qqnumber = s_strdup(qqnum);
        LwqqAsyncEvent* ev = lwqq_info_search_friend(ac->qq,uni_id,friend);
#if PURPLE_OUTDATE
        const char* msg = NULL;
#else
        const char* msg = message;
#endif
        lwqq_async_add_event_listener(ev, _C_(4p,search_buddy_receipt,ev,friend,s_strdup(uni_id),s_strdup(msg)));
    }
}
//#endif
static gboolean qq_send_attention(PurpleConnection* gc,const char* username,guint type)
{
    qq_account* ac = gc->proto_data;
    LwqqClient* lc = ac->qq;
    lwqq_msg_shake_window(lc, local_id_to_serv(ac, username));
    return TRUE;
}
static char* qq_status_text(PurpleBuddy* pb)
{
    LwqqBuddy* buddy = pb->proto_data;
    if(!buddy||!buddy->long_nick) return NULL;
    return translate_to_html_symbol(buddy->long_nick);
}
static void qq_tooltip_text(PurpleBuddy* pb,PurpleNotifyUserInfo* info,gboolean full)
{
    char buf[32];
    LwqqBuddy* buddy = pb->proto_data;
    if(!buddy) return;
    if(buddy->qqnumber)
        purple_notify_user_info_add_pair(info, "QQ", buddy->qqnumber);
    if(buddy->nick)
        purple_notify_user_info_add_pair(info, _("Nick"), buddy->nick);
    if(buddy->markname)
        purple_notify_user_info_add_pair(info, _("Mark"), buddy->markname);
    if(buddy->long_nick)
        purple_notify_user_info_add_pair(info, _("Longnick"), buddy->long_nick);
    snprintf(buf,sizeof(buf),"%d",buddy->level);
    purple_notify_user_info_add_pair(info, _("Level"), buf);
    purple_notify_user_info_add_pair(info, _("Client"), qq_client_to_str(buddy->client_type));
}
#if 0
static void qq_visit_qun_air(PurpleBlistNode* node)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    GHashTable* table = purple_chat_get_components(chat);
    const char* qqnum = g_hash_table_lookup(table,QQ_ROOM_KEY_QUN_ID);
    char url[256]={0};
    snprintf(url,sizeof(url),"xdg-open 'http://qun.qq.com/air/%s'",qqnum);
    system(url);
}
#endif
static LwqqGroup* find_group_by_chat(PurpleChat* chat)
{
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    LwqqClient* lc = ac->qq;
    GHashTable* table = purple_chat_get_components(chat);
    if(ac->flag&QQ_USE_QQNUM){
        const char* qqnum = g_hash_table_lookup(table,QQ_ROOM_KEY_GID);
        return find_group_by_qqnumber(lc, qqnum);
    }else{
        const char* gid = g_hash_table_lookup(table,QQ_ROOM_KEY_GID);
        return find_group_by_gid(lc,gid);
    }
}

static void set_cgroup_block(LwqqConfirmTable* ct,LwqqClient* lc,LwqqGroup* g)
{
    if(ct->answer != LWQQ_IGNORE){
        lwqq_async_add_event_listener(
                lwqq_info_mask_group(lc,g,ct->answer),
                _C_(p,qq_set_group_name,g->data));
    }
    lwqq_ct_free(ct);
}
static void qq_block_chat(PurpleBlistNode* node)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    LwqqClient* lc = ac->qq;
    LwqqGroup* group = find_group_by_chat(chat);

    LwqqConfirmTable* ct = s_malloc0(sizeof(*ct));
    ct->title = s_strdup(_("Block Setting"));
    ct->no_label = s_strdup(_("No Block"));
    ct->yes_label = s_strdup(_("Slience Receive"));
    ct->exans_label = s_strdup(_("Block"));
    ct->flags |= LWQQ_CT_CHOICE_MODE|LWQQ_CT_ENABLE_IGNORE;
    ct->answer = group->mask;
    ct->cmd = _C_(3p,set_cgroup_block,ct,lc,group);
    show_confirm_table(lc, ct);

}
static void self_card_ok(vp_list* list,PurpleRequestFields* root)
{
    vp_start(*list);
    LwqqBusinessCard* c = vp_arg(*list,LwqqBusinessCard*);
    LwqqClient* lc = vp_arg(*list,LwqqClient*);
    vp_end(*list);
    s_free(list);
    const char* value = purple_request_fields_get_string(root, "name");
    if(value){ s_free(c->name); c->name = s_strdup(value);}
    value = purple_request_fields_get_string(root,"phone");
    if(value){ s_free(c->phone); c->phone = s_strdup(value);}
    value = purple_request_fields_get_string(root, "email");
    if(value){ s_free(c->email); c->email = s_strdup(value);}
    value = purple_request_fields_get_string(root, "remark");
    if(value){ s_free(c->remark); c->remark = s_strdup(value);}
    lwqq_info_set_self_card(lc, c);
    lwqq_card_free(c);
}
static void self_card_cancel(vp_list* list,PurpleRequestFields* root)
{
    vp_start(*list);
    LwqqBusinessCard* c = vp_arg(*list,LwqqBusinessCard*);
    vp_end(*list);
    s_free(list);
    lwqq_card_free(c);
}
static void qq_display_self_card(LwqqClient* lc,LwqqBusinessCard* card)
{
    PurpleRequestFields* fields = purple_request_fields_new();
    PurpleRequestFieldGroup* g = purple_request_field_group_new("cards");
    purple_request_fields_add_group(fields, g);
    PurpleRequestField* f;
    f = purple_request_field_string_new("name", _("Name"), card->name, FALSE);
    purple_request_field_group_add_field(g, f);
    f = purple_request_field_string_new("phone", _("Phone"), card->phone, FALSE);
    purple_request_field_group_add_field(g, f);
    f = purple_request_field_string_new("email", _("Email"), card->email, FALSE);
    purple_request_field_group_add_field(g, f);
    f = purple_request_field_string_new("remark", _("Remark"), card->remark, TRUE);
    purple_request_field_group_add_field(g, f);

    qq_account* ac = lc->data;
    purple_request_fields(ac->gc, _("Group Business Card"), NULL, NULL, fields,
            _("Update"), G_CALLBACK(self_card_ok), _("Cancel"), G_CALLBACK(self_card_cancel), 
            ac->account, NULL, NULL, _P_(2p,card,lc));
}
static void qq_set_self_card(PurpleBlistNode* node)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    LwqqClient* lc = ac->qq;
    LwqqGroup* group = find_group_by_chat(chat);
    LwqqBusinessCard* card = s_malloc0(sizeof(*card));
    LwqqAsyncEvent* ev = lwqq_info_get_self_card(lc, group, card);
    lwqq_async_add_event_listener(ev, _C_(2p,qq_display_self_card,lc,card));
}
static void qq_quit_group(PurpleBlistNode* node)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    LwqqClient* lc = ac->qq;
    LwqqGroup* group = find_group_by_chat(chat);
    lwqq_info_delete_group(lc, group);
}
static void set_group_alias_local(PurpleBlistNode* node,char* mark)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    purple_blist_alias_chat(chat, mark);
    s_free(mark);
}
static void set_group_alias(PurpleBlistNode* node,const char* mark)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    LwqqClient* lc = ac->qq;
    LwqqGroup* group = find_group_by_chat(chat);
    LwqqAsyncEvent* ev;
    if(group->type == LWQQ_GROUP_QUN)
        ev = lwqq_info_change_group_markname(lc, group, mark);
    else
        ev = lwqq_info_set_dicsu_topic(lc, group, mark);
    lwqq_async_add_event_listener(ev, _C_(2p,set_group_alias_local,node,s_strdup(mark)));
}
static void qq_set_group_alias(PurpleBlistNode* node)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    char* type = g_hash_table_lookup(chat->components,QQ_ROOM_TYPE);
    if(type == NULL) return;
    if(strcmp(type,QQ_ROOM_TYPE_QUN)==0)
        purple_request_input(ac->gc, _("Modify Mark"), _("Input Mark"), NULL, NULL, FALSE, FALSE, NULL, 
                _("Update"), G_CALLBACK(set_group_alias), _("Cancel"), G_CALLBACK(do_no_thing), ac->account, NULL, NULL, node);
    else purple_request_input(ac->gc,_("Set Topic"),_("Input Topic"),
            _("Note:You are setting discussion topic on server.\nWhich would affect all discussion member"),NULL,FALSE,FALSE,NULL,
            "设置",G_CALLBACK(set_group_alias),"取消",G_CALLBACK(do_no_thing),ac->account,NULL,NULL,node);
}
static void qq_get_group_info(PurpleBlistNode* node)
{
    PurpleChat* chat = PURPLE_CHAT(node);
    PurpleAccount* account = purple_chat_get_account(chat);
    qq_account* ac = purple_connection_get_protocol_data(purple_account_get_connection(account));
    LwqqGroup* g = find_group_by_chat(chat);
    PurpleNotifyUserInfo* info = purple_notify_user_info_new();
#define ADD_STRING(k,v) purple_notify_user_info_add_pair(info,k,v)
    ADD_STRING(_("QQ"),g->account);
    ADD_STRING(_("Name"),g->name);
    ADD_STRING(_("Mark"),g->markname);
    ADD_STRING(_("Longnick"),g->memo);
    //ADD_STRING("创建人",g->owner);
    //ADD_STRING("创建时间",ctime(&g->createtime));
    purple_notify_userinfo(ac->gc, g->account, info, (PurpleNotifyCloseCallback)purple_notify_user_info_destroy, info);
#undef ADD_STRING
}

static void merge_online_history(LwqqAsyncEvent* ev,LwqqBuddy* b,LwqqGroup* g,LwqqHistoryMsgList* history)
{
    LwqqClient* lc = ev->lc;
    qq_account* ac = lc->data;
    if(ev->result){
        char buf[64]={0};
        snprintf(buf,sizeof(buf),_("Error Code:%d"),ev->result);
        purple_notify_warning(ac->account,_("Error"),buf,NULL);
        return ;
    }
    if(history->total == 0) return ;
    LwqqRecvMsg* recv;
    LwqqMsgMessage* msg;
    char buf[BUFLEN]={0};
    const char* name = b?b->qqnumber:g->account;
    int type = b?PURPLE_LOG_IM:PURPLE_LOG_CHAT;

    recv = TAILQ_FIRST(&history->msg_list);
    msg = (LwqqMsgMessage*)recv->msg;
    time_t log_time = msg->time;
    struct tm log_tm = *localtime(&msg->time);

    //delete old log
    GList* old_logs = purple_log_get_logs(type, name, ac->account);
    while(old_logs){
        PurpleLog* log = old_logs->data;
        if(log->time >= log_time && purple_log_is_deletable(log))
            purple_log_delete(log);
        old_logs = old_logs->next;
    }

    PurpleLog* log = purple_log_new(type,name,ac->account,NULL,log_time,NULL);
    const char* another = b?(b->markname?:b->nick):NULL;
    const char* me = ac->account->alias;
    int count = 0;
    snprintf(buf,sizeof(buf),"======online history begin=======");
    purple_log_write(log,PURPLE_MESSAGE_SYSTEM,"system",log_time,buf);
    TAILQ_FOREACH(recv,&history->msg_list,entries){
        count++;
        //clear buf.
        buf[0]='\0';
        msg = (LwqqMsgMessage*)recv->msg;
        //if time interval is extend 1 day. we create a new log
        struct tm msg_tm = *localtime(&msg->time);
        if(msg_tm.tm_year!=log_tm.tm_year || msg_tm.tm_yday!=log_tm.tm_yday){
            snprintf(buf,sizeof(buf),"======online history end=======");
            purple_log_write(log,PURPLE_MESSAGE_SYSTEM,"system",log_time,buf);
            purple_log_free(log);
            log_time = msg->time;
            log_tm = *localtime(&msg->time);
            log = purple_log_new(b?PURPLE_LOG_IM:PURPLE_LOG_CHAT,name,ac->account,NULL,log_time,NULL);
            snprintf(buf,sizeof(buf),"======online history begin======");
            purple_log_write(log,PURPLE_MESSAGE_SYSTEM,"system",log_time,buf);
            buf[0]='\0';
        }
        translate_struct_to_message(ac, msg, buf);
        if(b){
            if(strcmp(msg->super.from,b->uin)==0){
                purple_log_write(log,PURPLE_MESSAGE_RECV,another,msg->time,buf);
            }else
                purple_log_write(log,PURPLE_MESSAGE_SEND,me,msg->time,buf);
        }else{
            LwqqSimpleBuddy* sb = lwqq_group_find_group_member_by_uin(g, msg->super.from);
            purple_log_write(log,PURPLE_MESSAGE_RECV,sb?(sb->card?:sb->nick):msg->super.from,msg->time,buf);
        }
    }
    snprintf(buf,sizeof(buf),"======online history end=======");
    purple_log_write(log,PURPLE_MESSAGE_SYSTEM,"system",log_time,buf);
    snprintf(buf,sizeof(buf),_("Merge %d Entry"),count);
    purple_notify_info(ac->gc,_("Merge Online History"),buf,NULL);
    lwqq_historymsg_free(history);
    purple_log_free(log);
}
static void download_online_history_continue(LwqqAsyncEvent* ev,LwqqBuddy* b,LwqqGroup* g,LwqqHistoryMsgList* history)
{
    LwqqClient* lc = ev->lc;
    if((b&&history->total==0)||(g&&history->end == -1)){
        qq_account* ac = lc->data;
        if(TAILQ_EMPTY(&history->msg_list)){
        purple_notify_info(ac->gc,_("Merge Online History"),_("Merge 0 Entry"),NULL);
        lwqq_historymsg_free(history);
        }else{
            merge_online_history(ev,NULL,g,history);
        }
        return;
    }
    LwqqAsyncEvent* event;
    if(b){
        history->page--;
        if(history->page==0){
            merge_online_history(ev, b, NULL, history);
            return;
        }
        event = lwqq_msg_friend_history(lc, b->uin, history);
    }else{
        history->begin-=60;
        history->end-=60;
        history->reserve--;
        if(history->reserve==0){
            merge_online_history(ev,NULL,g,history);
            return;
        }
        event = lwqq_msg_group_history(lc, g, history);

    }
    lwqq_async_add_event_listener(event, _C_(4p,download_online_history_continue,event,b,g,history));
}
static void download_online_history_begin(LwqqGroup* g,LwqqConfirmTable* ct,qq_account* ac)
{
    LwqqClient* lc = ac->qq;
    if(ct->answer == LWQQ_YES){
        char* end;
        int day = strtoul(ct->input,&end,10);
        if(end == ct->input){
            purple_notify_warning(ac->account,_("Error"),_("Input Invalid"),NULL);
        }else{
            LwqqHistoryMsgList* history = lwqq_historymsg_list();
            history->row = 60;
            history->begin = history->end = 0;
            history->reserve = day;
            LwqqAsyncEvent* ev = lwqq_msg_group_history(lc, g, history);
            if(LIST_EMPTY(&g->members)){
                LwqqAsyncEvset* set = lwqq_async_evset_new();
                lwqq_async_evset_add_event(set, ev);
                ev = lwqq_info_get_group_detail_info(lc, g, NULL);
                lwqq_async_evset_add_event(set, ev);
                lwqq_async_add_evset_listener(set, 
                        _C_(4p,download_online_history_continue,ev,NULL,g,history));
            }else
                lwqq_async_add_event_listener(ev, 
                        _C_(4p,download_online_history_continue,ev,NULL,g,history));
        }
    }
    lwqq_ct_free(ct);
}
static void qq_merge_online_history(PurpleBuddy* buddy)
{
    PurpleConnection* gc = buddy->account->gc;
    qq_account* ac = gc->proto_data;
    LwqqClient* lc = ac->qq;
    LwqqBuddy* b = buddy->proto_data;
    if(b == NULL){
        if(strstr(buddy->name," ### "))
            purple_notify_info(gc,_("Error"),_("Tempory Session not support this function"),NULL);
        return;
    }
    LwqqHistoryMsgList* history = lwqq_historymsg_list();
    history->row = 60;
    history->page = 0;
    LwqqAsyncEvent* ev = lwqq_msg_friend_history(lc, b->uin, history);
    lwqq_async_add_event_listener(ev,
            _C_(4p,download_online_history_continue,ev,b,NULL,history));
}
static void qq_merge_group_history(PurpleChat* chat)
{
    qq_account* ac = chat->account->gc->proto_data;
    LwqqClient* lc = ac->qq;
    LwqqGroup* g = find_group_by_chat(chat);
    if(g == NULL) return;

    LwqqConfirmTable* ct = s_malloc0(sizeof(*ct));
    ct->title = s_strdup(_("How many pages want to merge"));
    ct->body = s_strdup(_("Each Page has 60 Entry"));
    ct->input_label = s_strdup(_("Pages"));
    ct->cmd = _C_(3p,download_online_history_begin,g,ct,ac);
    show_confirm_table(lc, ct);
}
static GList* qq_blist_node_menu(PurpleBlistNode* node)
{
    GList* act = NULL;
    PurpleMenuAction* action;
    if(PURPLE_BLIST_NODE_IS_BUDDY(node)) {
        PurpleBuddy* buddy = PURPLE_BUDDY(node);
        //LwqqBuddy* friend = buddy->proto_data;
        action = purple_menu_action_new(_("Visit QZone"),(PurpleCallback)qq_visit_qzone,node,NULL);
        act = g_list_append(act,action);
        action = purple_menu_action_new(_("Send Offline File"),(PurpleCallback)qq_send_offline_file,node,NULL);
        act = g_list_append(act,action);
        action = purple_menu_action_new(_("Send Email"),(PurpleCallback)qq_send_mail,node,NULL);
        act  = g_list_append(act,action);
        action = purple_menu_action_new(_("Merge Online History"),(PurpleCallback)qq_merge_online_history,buddy,NULL);
        act = g_list_append(act,action);
    } else if(PURPLE_BLIST_NODE_IS_CHAT(node)) {
        PurpleChat* chat = PURPLE_CHAT(node);
        LwqqGroup* group = qq_get_group_from_chat(chat);
        if(group&&group->type == LWQQ_GROUP_QUN){
            action = purple_menu_action_new(_("Get Information"),(PurpleCallback)qq_get_group_info,node,NULL);
            act = g_list_append(act,action);
            action = purple_menu_action_new(_("Change Business Card"),(PurpleCallback)qq_set_self_card,node,NULL);
            act = g_list_append(act,action);
            action = purple_menu_action_new(_("Merge Online History"),(PurpleCallback)qq_merge_group_history,chat,NULL);
            act = g_list_append(act,action);
        }
        action = purple_menu_action_new(_("Block"),(PurpleCallback)qq_block_chat,node,NULL);
        act = g_list_append(act,action);
        action = purple_menu_action_new(_("Change Mark"),(PurpleCallback)qq_set_group_alias,node,NULL);
        act = g_list_append(act,action);
        action = purple_menu_action_new(_("Quit Group"), (PurpleCallback)qq_quit_group, node, NULL);
        act = g_list_append(act,action);
    }
    return act;
}
static void client_connect_signals(PurpleConnection* gc)
{
}
#if 0
static const char* qq_list_emblem(PurpleBuddy* b)
{
    LwqqBuddy* buddy = b->proto_data;
    const char* ret = NULL;
    switch(buddy->client_type){
        case LWQQ_CLIENT_WEBQQ:
            ret = "external";
            break;
        default:
            break;
    }
    return ret;
}
#endif

static void display_user_info(PurpleConnection* gc,LwqqBuddy* b,char *who)
{
    PurpleNotifyUserInfo* info = purple_notify_user_info_new();
    qq_account* ac = gc->proto_data;
#define ADD_INFO(k,v)   purple_notify_user_info_add_pair(info,k,v)
#define ADD_HEADER(s)     purple_notify_user_info_add_section_break(info)
//#define ADD_HEADER(s)     purple_notify_user_info_add_section_header(info, s)
    //ADD_HEADER("基本信息");
    ADD_INFO(_("QQ"),b->qqnumber);
    ADD_INFO(_("Nick"),b->nick);
    ADD_INFO(_("Mark"),b->markname);
    ADD_INFO(_("Longnick"),b->long_nick);
    ADD_INFO(_("Level"),qq_level_to_str(b->level));
    ADD_HEADER(_("Self Information"));
    ADD_INFO(_("Gender"), qq_gender_to_str(b->gender));
    ADD_INFO(_("Shengxiao"),qq_shengxiao_to_str(b->shengxiao));
    ADD_INFO(_("Constellation"),qq_constel_to_str(b->constel));
    ADD_INFO(_("Blood Type"),qq_blood_to_str(b->blood));
    ADD_INFO(_("Birthday"),lwqq_date_to_str(b->birthday));
    ADD_HEADER(_("Place Information"));
    ADD_INFO(_("Contry"),b->country);
    ADD_INFO(_("Province"),b->province);
    ADD_INFO(_("City"),b->city);
    ADD_HEADER(_("Conmunicate Information"));
    ADD_INFO(_("Phone"),b->phone);
    ADD_INFO(_("Mobile"),b->mobile);
    ADD_INFO(_("Email"),b->email);
    ADD_INFO(_("Occupation"),b->occupation);
    ADD_INFO(_("College"),b->college);
    ADD_INFO(_("Homepage"),b->homepage);
    //ADD_INFO("简介","");
    purple_notify_userinfo(gc, who?who:try_get(b->qqnumber,b->uin), info, (PurpleNotifyCloseCallback)purple_notify_user_info_destroy, info);
    //if who is not NULL,this is a group member 
    //detail info display.we should free buddy.
    if(who) {
        lwqq_buddy_free(b);
        s_free(who);
    }else{
        lwdb_userdb_update_buddy_info(ac->db, b);
    }
#undef ADD_INFO
#undef ADD_HEADER
}
static void qq_get_user_info(PurpleConnection* gc,const char* who)
{
    qq_account* ac = gc->proto_data;
    LwqqClient* lc = ac->qq;
    LwqqBuddy* buddy;
    if(ac->flag&QQ_USE_QQNUM)
        buddy = lc->find_buddy_by_qqnumber(lc,who);
    else 
        buddy = lc->find_buddy_by_uin(lc,who);
    if(buddy){
        LwqqAsyncEvent* ev = lwqq_info_get_friend_detail_info(lc, buddy);
        lwqq_async_add_event_listener(ev, _C_(3p,display_user_info,gc,buddy,NULL));
    }else{
        // Not a buddy? try fetch stranger info
        LwqqGroup* g = NULL;
        LwqqSimpleBuddy* sb = NULL;
        if(find_group_and_member_by_card(lc,who, &g, &sb)){
            buddy = lwqq_buddy_new();
            buddy->uin = s_strdup(sb->uin);

            LwqqAsyncEvset* set = lwqq_async_evset_new();
            LwqqAsyncEvent* ev = NULL;
            ev = lwqq_info_get_group_member_detail(lc,sb->uin,buddy);
            lwqq_async_evset_add_event(set, ev);
            ev = lwqq_info_get_friend_qqnumber(lc,buddy);
            lwqq_async_evset_add_event(set, ev);
            lwqq_async_add_evset_listener(set, _C_(3p,display_user_info,gc,buddy,s_strdup(who)));
        }
    }
}
static void qq_add_buddies_to_discu(PurpleConnection* gc,int id,const char* message,const char* local_id)
{
    qq_account* ac = gc->proto_data;
    LwqqClient* lc = ac->qq;
    LwqqGroup* discu = opend_chat_index(ac,id);
    if(discu->type != LWQQ_GROUP_DISCU){
        purple_notify_info(gc,_("Error"),_("Only Discussion Can Add new member"),NULL);
        return;
    }
    LwqqBuddy* b = NULL;
    if(ac->flag&QQ_USE_QQNUM)
        b = find_buddy_by_qqnumber(lc, local_id);
    else
        b = find_buddy_by_uin(lc, local_id);
    if(b == NULL)
        b = lwqq_buddy_find_buddy_by_name(lc, local_id);

    LwqqDiscuMemChange* chg = lwqq_discu_mem_change_new();
    lwqq_discu_add_buddy(chg, b);

    lwqq_info_change_discu_mem(lc,discu, chg);
}

PurplePluginProtocolInfo webqq_prpl_info = {
    /* options */
#if PURPLE_OUTDATE
    .options=           OPT_PROTO_IM_IMAGE|OPT_PROTO_CHAT_TOPIC,
#else
    .options=           OPT_PROTO_IM_IMAGE|OPT_PROTO_INVITE_MESSAGE|OPT_PROTO_CHAT_TOPIC,
#endif
    .icon_spec=         {"jpg,jpeg,gif,png", 0, 0, 96, 96, 0, PURPLE_ICON_SCALE_SEND},
    .list_icon=         qq_list_icon,
    .login=             qq_login,
    .close=             qq_close,
    .status_text=       qq_status_text,
    .tooltip_text=      qq_tooltip_text,
    .status_types=      qq_status_types,
    .set_status=        qq_set_status,
    .blist_node_menu=   qq_blist_node_menu,
    //.list_emblem=       qq_list_emblem,
    .get_info=          qq_get_user_info,
    /**group part start*/
    .chat_info=         qq_chat_info,    /* chat_info implement this to enable chat*/
    .chat_info_defaults=qq_chat_info_defaults, /* chat_info_defaults */
    .roomlist_get_list =qq_get_all_group_list,
    .join_chat=         qq_group_add_or_join,
    .chat_invite=       qq_add_buddies_to_discu,
    //.set_chat_topic=    qq_set_chat_topic,
    .get_cb_real_name=  qq_get_cb_real_name,
    /**group part end*/
    .send_im=           qq_send_im,     /* send_im */
    .send_typing=       qq_send_typing,
    .chat_send=         qq_send_chat,
    .send_attention=    qq_send_attention,
    //.chat_leave=        qq_leave_chat,
    .send_file=         qq_send_file,
    //.chat_whisper=      qq_send_whisper,
    .offline_message=   qq_offline_message,
    .alias_buddy=       qq_change_markname, /* change buddy alias on server */
    .group_buddy=       qq_change_category  /* change buddy category on server */,
    .rename_group=      qq_rename_category,
    .remove_buddy=      qq_remove_buddy,
#if PURPLE_OUTDATE
    .add_buddy=         qq_add_buddy,
#else
    .add_buddy_with_invite=qq_add_buddy,
#endif

    .struct_size=       sizeof(PurplePluginProtocolInfo)
};

static PurplePluginInfo info = {
    .magic=         PURPLE_PLUGIN_MAGIC,
    .major_version= PURPLE_MAJOR_VERSION,
    .minor_version= PURPLE_MINOR_VERSION,
    .type=          PURPLE_PLUGIN_PROTOCOL, /* type */
    .flags=         0, /* flags */
    .priority=      PURPLE_PRIORITY_DEFAULT, /* priority */
    .id=            "prpl-webqq", /* id */
    .name=          "WebQQ", /* name */
    .version=       "0.1f", /* version */
    .summary=       "WebQQ Protocol Plugin", /* summary */
    .description=   "a webqq plugin based on lwqq", /* description */
    .author=        "xiehuc<xiehuc@gmail.com>", /* author */
    .homepage=      "https://github.com/xiehuc/pidgin-lwqq",
    .extra_info=    &webqq_prpl_info, /* extra_info */
    .actions=       plugin_actions_menu,
};

static void
init_plugin(PurplePlugin *plugin)
{
#ifdef ENABLE_NLS
    setlocale(LC_ALL, "");
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    bindtextdomain(GETTEXT_PACKAGE , LOCALEDIR);
    textdomain(GETTEXT_PACKAGE);
#endif

    PurpleAccountOption *option;
    GList* options = NULL;
    option = purple_account_option_bool_new(_("Ignore Received Message Font Family"), "disable_custom_font_face", FALSE);
    options = g_list_append(options, option);
    option = purple_account_option_bool_new(_("Ignore Received Message Font Size"), "disable_custom_font_size", FALSE);
    options = g_list_append(options, option);
    option = purple_account_option_bool_new(_("Text Lighter In Dark Theme"), "dark_theme_fix", FALSE);
    options = g_list_append(options, option);
    option = purple_account_option_bool_new(_("Remove Duplicated Message"),"remove_duplicated_msg",FALSE);
    options = g_list_append(options, option);
    option = purple_account_option_bool_new(_("Don't Download Group Pic(Reduce Network Transfer)"), "no_download_group_pic", FALSE);
    options = g_list_append(options,option);
    option = purple_account_option_bool_new(_("Version Statics"), "version_statics", TRUE);
    options = g_list_append(options, option);
    option = purple_account_option_bool_new(_("Debug File Transport"), "debug_file_send", FALSE);
    options = g_list_append(options, option);
    option = purple_account_option_bool_new(_("Do not use Expected:100 Continue when send offline message"),"dont_expected_100_continue",FALSE);
    options = g_list_append(options, option);
    option = purple_account_option_string_new(_("Ignore Group(Compatible with recent plugin)"), "recent_group_name", "Recent Contacts");
    options = g_list_append(options, option);
    option = purple_account_option_int_new(_("Verbose"), "verbose", 0);
    options = g_list_append(options, option);
    option = purple_account_option_int_new(_("Send Relink Time Interval(m)"), "relink_retry", 0);
    options = g_list_append(options, option);

    webqq_prpl_info.protocol_options = options;

}

static void version_statics(qq_account* ac,LwqqConfirmTable* ct)
{
    int ans = 1;
    if(ct){
        ans = ct->answer;
        purple_account_set_bool(ac->account, "version_statics", ans);
        lwqq_ct_free(ct);
    }else{
        ans = purple_account_get_bool(ac->account, "version_statics", TRUE);
    }
    if(ans){
        const char* url = "http://pidginlwqq.sinaapp.com/statics.php";
        char post[128];
        snprintf(post,sizeof(post),"v=%s",info.version);
        LwqqHttpRequest *req = lwqq_http_request_new(url);
        lwqq_http_set_option(req, LWQQ_HTTP_NOT_SET_COOKIE,1L);
        req->do_request_async(req,1,post,_C_(p,lwqq_http_request_free,req));
    }
}
static void version_statics_dlg(qq_account* ac)
{
    const char* v = lwdb_userdb_read(ac->db, "v");
    if(v == NULL){
        LwqqConfirmTable* ct = s_malloc0(sizeof(*ct));
        ct->title = s_strdup(_("Need Your Attention"));
        char body[1024];
        snprintf(body,sizeof(body),_("To Support the sustained development of pidgin-lwqq,\n"
                "Need statistics version count!Really Just a version number only!!,following releated information:\n"
                "Note:https://github.com/xiehuc/pidgin-lwqq/wiki/version-statistics\n"
                "Frequent:First run of each version\n"
                "Url:http://pidginlwqq.sinaapp.com/statics.php\n"
                "POST:v=%s\n"
                "Code:%s:%d\n"),
                info.version,_FILE_NAME_,__LINE__);
        ct->body = s_strdup(body);
        ct->yes_label = s_strdup(_("Agree"));
        ct->no_label = s_strdup(_("Deny"));
        ct->cmd = _C_(2p,version_statics,ac,ct);
        show_confirm_table(ac->qq, ct);
        lwdb_userdb_write(ac->db, "v", info.version);
    }else if(strcmp(v,info.version)!=0){
        version_statics(ac,NULL);
        lwdb_userdb_write(ac->db, "v", info.version);
    }
}

static
TABLE_BEGIN(proxy_map,long,0)
    TR(PURPLE_PROXY_USE_GLOBAL, LWQQ_HTTP_PROXY_NOT_SET)
    TR(PURPLE_PROXY_USE_ENVVAR, LWQQ_HTTP_PROXY_NOT_SET)
    TR(PURPLE_PROXY_NONE,       LWQQ_HTTP_PROXY_NONE)
    TR(PURPLE_PROXY_HTTP,       LWQQ_HTTP_PROXY_HTTP)
    TR(PURPLE_PROXY_SOCKS4,     LWQQ_HTTP_PROXY_SOCKS4)
    TR(PURPLE_PROXY_SOCKS5,     LWQQ_HTTP_PROXY_SOCKS5)
TABLE_END()

static int relink_keepalive(void* data)
{
    qq_account* ac = data;
    lwqq_relink(ac->qq);
    return 1;
}

static void qq_login(PurpleAccount *account)
{
    PurpleConnection* pc= purple_account_get_connection(account);
    qq_account* ac = qq_account_new(account);
    const char* username = purple_account_get_username(account);
    const char* password = purple_account_get_password(account);
    if(password==NULL||strcmp(password,"")==0) {
        purple_connection_error_reason(pc,PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED,_("Password is empty"));
        return;
    }
    g_ref_count ++ ;
    ac->gc = pc;
    ac->qq->async_opt = &qq_async_opt;
    lwqq_bit_set(ac->flag,IGNORE_FONT_SIZE,purple_account_get_bool(account, "disable_custom_font_size", FALSE));
    lwqq_bit_set(ac->flag, IGNORE_FONT_FACE, purple_account_get_bool(account, "disable_custom_font_face", FALSE));
    lwqq_bit_set(ac->flag, DARK_THEME_ADAPT, purple_account_get_bool(account, "dark_theme_fix", FALSE));
    lwqq_bit_set(ac->flag, DEBUG_FILE_SEND, purple_account_get_bool(account,"debug_file_send",FALSE));
    lwqq_bit_set(ac->flag, REMOVE_DUPLICATED_MSG, purple_account_get_bool(account,"remove_duplicated_msg",FALSE));
    lwqq_bit_set(ac->flag, QQ_DONT_EXPECT_100_CONTINUE,purple_account_get_bool(account,"dont_expected_100_continue",FALSE));
    lwqq_bit_set(ac->flag, NOT_DOWNLOAD_GROUP_PIC, purple_account_get_bool(account, "no_download_group_pic", FALSE));
    ac->recent_group_name = s_strdup(purple_account_get_string(account, "recent_group_name", "Recent Contacts"));
    int relink_retry = 0;
    if((relink_retry = purple_account_get_int(account, "relink_retry", 0))>0)
        ac->relink_timer = purple_timeout_add_seconds(relink_retry*60, relink_keepalive, ac);
    lwqq_log_set_level(purple_account_get_int(account,"verbose",0));
    char db_path[64]={0};
    snprintf(db_path,sizeof(db_path),"%s/.config/lwqq",getenv("HOME"));
    ac->db = lwdb_userdb_new(username,db_path,0);
    ac->qq->data = ac;
    //for empathy
    lwqq_bit_set(ac->flag,QQ_USE_QQNUM,ac->db!=NULL);
    purple_buddy_icons_set_caching(lwqq_bit_get(ac->flag, QQ_USE_QQNUM));
    if(ac->db){
        version_statics_dlg(ac);
        lwqq_override(ac->font.family,s_strdup(lwdb_userdb_read(ac->db, "f_family")));
        ac->font.size = s_atoi(lwdb_userdb_read(ac->db,"f_size"),ac->font.size);
        ac->font.style = s_atoi(lwdb_userdb_read(ac->db,"f_style"),ac->font.style);
    }
    
    if(!ac->flag&QQ_USE_QQNUM) 
        all_reset(ac,RESET_ALL);

    purple_connection_set_protocol_data(pc,ac);
    client_connect_signals(ac->gc);

    PurpleProxyInfo* proxy = purple_proxy_get_setup(ac->account);
    lwqq_http_proxy_set(lwqq_get_http_handle(ac->qq),proxy_map(proxy->type),proxy->host,proxy->port,proxy->username,proxy->password);
    
    const char* status = purple_status_get_id(purple_account_get_active_status(ac->account));
    lwqq_login(ac->qq, lwqq_status_from_str(status), NULL);
}

PURPLE_INIT_PLUGIN(webqq, init_plugin, info)

