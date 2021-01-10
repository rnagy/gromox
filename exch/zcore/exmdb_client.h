#pragma once
#include <gromox/defs.h>
#include <gromox/mapi_types.hpp>
#include <gromox/element_data.hpp>

enum {
	ALIVE_PROXY_CONNECTIONS,
	LOST_PROXY_CONNECTIONS
};

struct EXMDB_REQUEST;
struct EXMDB_RESPONSE;

int exmdb_client_get_param(int param);

void exmdb_client_init(int conn_num,
	int threads_num, const char *list_path);
extern int exmdb_client_run(void);
extern int exmdb_client_stop(void);
extern void exmdb_client_free(void);
BOOL exmdb_client_get_named_propid(const char *dir,
	BOOL b_create, const PROPERTY_NAME *ppropname,
	uint16_t *ppropid);

BOOL exmdb_client_get_named_propname(const char *dir,
	uint16_t propid, PROPERTY_NAME *ppropname);

BOOL exmdb_client_get_folder_property(const char *dir,
	uint32_t cpid, uint64_t folder_id,
	uint32_t proptag, void **ppval);

BOOL exmdb_client_get_message_property(const char *dir,
	const char *username, uint32_t cpid, uint64_t message_id,
	uint32_t proptag, void **ppval);

BOOL exmdb_client_delete_message(const char *dir,
	int account_id, uint32_t cpid, uint64_t folder_id,
	uint64_t message_id, BOOL b_hard, BOOL *pb_done);

BOOL exmdb_client_get_instance_property(
	const char *dir, uint32_t instance_id,
	uint32_t proptag, void **ppval);

BOOL exmdb_client_set_instance_property(
	const char *dir, uint32_t instance_id,
	const TAGGED_PROPVAL *ppropval, uint32_t *presult);

BOOL exmdb_client_remove_instance_property(const char *dir,
	uint32_t instance_id, uint32_t proptag, uint32_t *presult);

BOOL exmdb_client_check_message_owner(const char *dir,
	uint64_t message_id, const char *username, BOOL *pb_owner);

BOOL exmdb_client_remove_message_property(const char *dir,
	uint32_t cpid, uint64_t message_id, uint32_t proptag);

void exmdb_client_register_proc(void *pproc);
extern BOOL exmdb_client_do_rpc(const char *dir, const EXMDB_REQUEST *, EXMDB_RESPONSE *);

#define IDLOUT
#define EXMIDL(n) extern BOOL exmdb_client_ ## n
EXMIDL(ping_store)(const char *dir);
EXMIDL(get_all_named_propids)(const char *dir, IDLOUT PROPID_ARRAY *propids);
EXMIDL(get_named_propids)(const char *dir, BOOL b_create, const PROPNAME_ARRAY *ppropnames, IDLOUT PROPID_ARRAY *propids);
EXMIDL(get_named_propnames)(const char *dir, const PROPID_ARRAY *ppropids, IDLOUT PROPNAME_ARRAY *propnames);
EXMIDL(get_mapping_guid)(const char *dir, uint16_t replid, IDLOUT BOOL *b_found, GUID *guid);
EXMIDL(get_mapping_replid)(const char *dir, GUID guid, IDLOUT BOOL *b_found, uint16_t *replid);
EXMIDL(get_store_all_proptags)(const char *dir, IDLOUT PROPTAG_ARRAY *proptags);
EXMIDL(get_store_properties)(const char *dir,	uint32_t cpid, const PROPTAG_ARRAY *pproptags, IDLOUT TPROPVAL_ARRAY *propvals);
EXMIDL(set_store_properties)(const char *dir, uint32_t cpid, const TPROPVAL_ARRAY *ppropvals, IDLOUT PROBLEM_ARRAY *problems);
EXMIDL(remove_store_properties)(const char *dir, const PROPTAG_ARRAY *pproptags);
EXMIDL(check_mailbox_permission)(const char *dir, const char *username, IDLOUT uint32_t *permission);
EXMIDL(get_folder_by_class)(const char *dir, const char *str_class, IDLOUT uint64_t *id, char *str_explicit);
EXMIDL(set_folder_by_class)(const char *dir, uint64_t folder_id, const char *str_class, IDLOUT BOOL *b_result);
EXMIDL(get_folder_class_table)(const char *dir, IDLOUT TARRAY_SET *table);
EXMIDL(check_folder_id)(const char *dir, uint64_t folder_id, IDLOUT BOOL *b_exist);
EXMIDL(check_folder_deleted)(const char *dir, uint64_t folder_id, IDLOUT BOOL *b_del);
EXMIDL(get_folder_by_name)(const char *dir, uint64_t parent_id, const char *str_name, IDLOUT uint64_t *folder_id);
EXMIDL(check_folder_permission)(const char *dir, uint64_t folder_id, const char *username, IDLOUT uint32_t *permission);
EXMIDL(create_folder_by_properties)(const char *dir, uint32_t cpid, const TPROPVAL_ARRAY *pproperties, IDLOUT uint64_t *folder_id);
EXMIDL(get_folder_all_proptags)(const char *dir, uint64_t folder_id, IDLOUT PROPTAG_ARRAY *proptags);
EXMIDL(get_folder_properties)(const char *dir, uint32_t cpid, uint64_t folder_id, const PROPTAG_ARRAY *pproptags, IDLOUT TPROPVAL_ARRAY *propvals);
EXMIDL(set_folder_properties)(const char *dir, uint32_t cpid, uint64_t folder_id, const TPROPVAL_ARRAY *pproperties, IDLOUT PROBLEM_ARRAY *problems);
EXMIDL(remove_folder_properties)(const char *dir, uint64_t folder_id, const PROPTAG_ARRAY *pproptags);
EXMIDL(delete_folder)(const char *dir, uint32_t cpid, uint64_t folder_id, BOOL b_hard, IDLOUT BOOL *b_result);
EXMIDL(empty_folder)(const char *dir, uint32_t cpid, const char *username, uint64_t folder_id, BOOL b_hard, BOOL b_normal, BOOL b_fai, BOOL b_sub, IDLOUT BOOL *b_partial);
EXMIDL(check_folder_cycle)(const char *dir, uint64_t src_fid, uint64_t dst_fid, IDLOUT BOOL *b_cycle);
EXMIDL(copy_folder_internal)(const char *dir, int account_id, uint32_t cpid, BOOL b_guest, const char *username, uint64_t src_fid, BOOL b_normal, BOOL b_fai, BOOL b_sub, uint64_t dst_fid, IDLOUT BOOL *b_collid, BOOL *b_partial);
EXMIDL(get_search_criteria)(const char *dir, uint64_t folder_id, IDLOUT uint32_t *search_status, RESTRICTION **prestriction, LONGLONG_ARRAY *folder_ids);
EXMIDL(set_search_criteria)(const char *dir, uint32_t cpid, uint64_t folder_id, uint32_t search_flags, const RESTRICTION *prestriction, const LONGLONG_ARRAY *pfolder_ids, IDLOUT BOOL *b_result);
EXMIDL(movecopy_message)(const char *dir, int account_id, uint32_t cpid, uint64_t message_id, uint64_t dst_fid, uint64_t dst_id, BOOL b_move, IDLOUT BOOL *b_result);
EXMIDL(movecopy_messages)(const char *dir, int account_id, uint32_t cpid, BOOL b_guest, const char *username, uint64_t src_fid, uint64_t dst_fid, BOOL b_copy, const EID_ARRAY *pmessage_ids, IDLOUT BOOL *b_partial);
EXMIDL(movecopy_folder)(const char *dir, int account_id, uint32_t cpid, BOOL b_guest, const char *username, uint64_t src_pid, uint64_t src_fid, uint64_t dst_fid, const char *str_new, BOOL b_copy, IDLOUT BOOL *b_exist, BOOL *b_partial);
EXMIDL(delete_messages)(const char *dir, int account_id, uint32_t cpid, const char *username, uint64_t folder_id, const EID_ARRAY *pmessage_ids, BOOL b_hard, IDLOUT BOOL *b_partial);
EXMIDL(get_message_brief)(const char *dir, uint32_t cpid, uint64_t message_id, IDLOUT MESSAGE_CONTENT **pbrief);
EXMIDL(sum_hierarchy)(const char *dir, uint64_t folder_id, const char *username, BOOL b_depth, IDLOUT uint32_t *count);
EXMIDL(load_hierarchy_table)(const char *dir, uint64_t folder_id, const char *username, uint8_t table_flags, const RESTRICTION *prestriction, IDLOUT uint32_t *table_id, uint32_t *row_count);
EXMIDL(sum_content)(const char *dir, uint64_t folder_id, BOOL b_fai, BOOL b_deleted, IDLOUT uint32_t *count);
EXMIDL(load_content_table)(const char *dir, uint32_t cpid, uint64_t folder_id, const char *username, uint8_t table_flags, const RESTRICTION *prestriction, const SORTORDER_SET *psorts, IDLOUT uint32_t *table_id, uint32_t *row_count);
EXMIDL(reload_content_table)(const char *dir, uint32_t table_id);
EXMIDL(load_permission_table)(const char *dir, uint64_t folder_id, uint8_t table_flags, IDLOUT uint32_t *table_id, uint32_t *row_count);
EXMIDL(load_rule_table)(const char *dir, uint64_t folder_id,  uint8_t table_flags, const RESTRICTION *prestriction, IDLOUT uint32_t *table_id, uint32_t *row_count);
EXMIDL(unload_table)(const char *dir, uint32_t table_id);
EXMIDL(sum_table)(const char *dir, uint32_t table_id, IDLOUT uint32_t *rows);
EXMIDL(query_table)(const char *dir, const char *username, uint32_t cpid, uint32_t table_id, const PROPTAG_ARRAY *pproptags, uint32_t start_pos, int32_t row_needed, IDLOUT TARRAY_SET *set);
EXMIDL(match_table)(const char *dir, const char *username, uint32_t cpid, uint32_t table_id, BOOL b_forward, uint32_t start_pos, const RESTRICTION *pres, const PROPTAG_ARRAY *pproptags, IDLOUT int32_t *position, TPROPVAL_ARRAY *propvals);
EXMIDL(locate_table)(const char *dir, uint32_t table_id, uint64_t inst_id, uint32_t inst_num, IDLOUT int32_t *position, uint32_t *row_type);
EXMIDL(read_table_row)(const char *dir, const char *username, uint32_t cpid, uint32_t table_id, const PROPTAG_ARRAY *pproptags, uint64_t inst_id, uint32_t inst_num, IDLOUT TPROPVAL_ARRAY *propvals);
EXMIDL(mark_table)(const char *dir, uint32_t table_id, uint32_t position, IDLOUT uint64_t *inst_id, uint32_t *inst_num, uint32_t *row_type);
EXMIDL(get_table_all_proptags)(const char *dir, uint32_t table_id, IDLOUT PROPTAG_ARRAY *proptags);
EXMIDL(expand_table)(const char *dir, uint32_t table_id, uint64_t inst_id, IDLOUT BOOL *b_found, int32_t *position, uint32_t *row_count);
EXMIDL(collapse_table)(const char *dir, uint32_t table_id, uint64_t inst_id, IDLOUT BOOL *b_found, int32_t *position, uint32_t *row_count);
EXMIDL(store_table_state)(const char *dir, uint32_t table_id, uint64_t inst_id, uint32_t inst_num, IDLOUT uint32_t *state_id);
EXMIDL(restore_table_state)(const char *dir, uint32_t table_id, uint32_t state_id, IDLOUT int32_t *position);
EXMIDL(check_message)(const char *dir, uint64_t folder_id, uint64_t message_id, IDLOUT BOOL *b_exist);
EXMIDL(check_message_deleted)(const char *dir, uint64_t message_id, IDLOUT BOOL *b_del);
EXMIDL(load_message_instance)(const char *dir, const char *username, uint32_t cpid, BOOL b_new, uint64_t folder_id, uint64_t message_id, IDLOUT uint32_t *instance_id);
EXMIDL(load_embedded_instance)(const char *dir, BOOL b_new, uint32_t attachment_instance_id, IDLOUT uint32_t *instance_id);
EXMIDL(get_embedded_cn)(const char *dir, uint32_t instance_id, IDLOUT uint64_t **pcn);
EXMIDL(reload_message_instance)(const char *dir, uint32_t instance_id, IDLOUT BOOL *b_result);
EXMIDL(clear_message_instance)(const char *dir, uint32_t instance_id);
EXMIDL(read_message_instance)(const char *dir, uint32_t instance_id, IDLOUT MESSAGE_CONTENT *msgctnt);
EXMIDL(write_message_instance)(const char *dir, uint32_t instance_id, const MESSAGE_CONTENT *pmsgctnt, BOOL b_force, IDLOUT PROPTAG_ARRAY *proptags, PROBLEM_ARRAY *problems);
EXMIDL(load_attachment_instance)(const char *dir, uint32_t message_instance_id, uint32_t attachment_num, IDLOUT uint32_t *instance_id);
EXMIDL(create_attachment_instance)(const char *dir, uint32_t message_instance_id, IDLOUT uint32_t *instance_id, uint32_t *attachment_num);
EXMIDL(read_attachment_instance)(const char *dir, uint32_t instance_id, IDLOUT ATTACHMENT_CONTENT *attctnt);
EXMIDL(write_attachment_instance)(const char *dir, uint32_t instance_id, const ATTACHMENT_CONTENT *pattctnt, BOOL b_force, IDLOUT PROBLEM_ARRAY *problems);
EXMIDL(delete_message_instance_attachment)(const char *dir, uint32_t message_instance_id, uint32_t attachment_num);
EXMIDL(flush_instance)(const char *dir, uint32_t instance_id, const char *account, IDLOUT gxerr_t *e_result);
EXMIDL(unload_instance)(const char *dir, uint32_t instance_id);
EXMIDL(get_instance_all_proptags)(const char *dir, uint32_t instance_id, IDLOUT PROPTAG_ARRAY *proptags);
EXMIDL(get_instance_properties)(const char *dir, uint32_t size_limit, uint32_t instance_id, const PROPTAG_ARRAY *pproptags, IDLOUT TPROPVAL_ARRAY *propvals);
EXMIDL(set_instance_properties)(const char *dir, uint32_t instance_id, const TPROPVAL_ARRAY *pproperties, IDLOUT PROBLEM_ARRAY *problems);
EXMIDL(remove_instance_properties)(const char *dir, uint32_t instance_id, const PROPTAG_ARRAY *pproptags, IDLOUT PROBLEM_ARRAY *problems);
EXMIDL(check_instance_cycle)(const char *dir, uint32_t src_instance_id, uint32_t dst_instance_id, IDLOUT BOOL *b_cycle);
EXMIDL(empty_message_instance_rcpts)(const char *dir, uint32_t instance_id);
EXMIDL(get_message_instance_rcpts_num)(const char *dir, uint32_t instance_id, IDLOUT uint16_t *num);
EXMIDL(get_message_instance_rcpts_all_proptags)(const char *dir, uint32_t instance_id, IDLOUT PROPTAG_ARRAY *proptags);
EXMIDL(get_message_instance_rcpts)(const char *dir, uint32_t instance_id, uint32_t row_id, uint16_t need_count, IDLOUT TARRAY_SET *set);
EXMIDL(update_message_instance_rcpts)(const char *dir, uint32_t instance_id, const TARRAY_SET *pset);
EXMIDL(copy_instance_rcpts)(const char *dir, BOOL b_force, uint32_t src_instance_id, uint32_t dst_instance_id, IDLOUT BOOL *b_result);
EXMIDL(empty_message_instance_attachments)(const char *dir, uint32_t instance_id);
EXMIDL(get_message_instance_attachments_num)(const char *dir, uint32_t instance_id, IDLOUT uint16_t *num);
EXMIDL(get_message_instance_attachment_table_all_proptags)(const char *dir, uint32_t instance_id, IDLOUT PROPTAG_ARRAY *proptags);
EXMIDL(query_message_instance_attachment_table)(const char *dir, uint32_t instance_id, const PROPTAG_ARRAY *pproptags, uint32_t start_pos, int32_t row_needed, IDLOUT TARRAY_SET *set);
EXMIDL(copy_instance_attachments)(const char *dir, BOOL b_force, uint32_t src_instance_id, uint32_t dst_instance_id, IDLOUT BOOL *b_result);
EXMIDL(set_message_instance_conflict)(const char *dir, uint32_t instance_id, const MESSAGE_CONTENT *pmsgctnt);
EXMIDL(get_message_rcpts)(const char *dir, uint64_t message_id, IDLOUT TARRAY_SET *set);
EXMIDL(get_message_properties)(const char *dir, const char *username, uint32_t cpid, uint64_t message_id, const PROPTAG_ARRAY *pproptags, IDLOUT TPROPVAL_ARRAY *propvals);
EXMIDL(set_message_properties)(const char *dir, const char *username, uint32_t cpid, uint64_t message_id, const TPROPVAL_ARRAY *pproperties, IDLOUT PROBLEM_ARRAY *problems);
EXMIDL(set_message_read_state)(const char *dir, const char *username, uint64_t message_id, uint8_t mark_as_read, IDLOUT uint64_t *read_cn);
EXMIDL(remove_message_properties)(const char *dir, uint32_t cpid, uint64_t message_id, const PROPTAG_ARRAY *pproptags);
EXMIDL(allocate_message_id)(const char *dir, uint64_t folder_id, IDLOUT uint64_t *message_id);
EXMIDL(allocate_cn)(const char *dir, IDLOUT uint64_t *cn);
EXMIDL(get_message_group_id)(const char *dir, uint64_t message_id, IDLOUT uint32_t **pgroup_id);
EXMIDL(set_message_group_id)(const char *dir, uint64_t message_id, uint32_t group_id);
EXMIDL(save_change_indices)(const char *dir, uint64_t message_id, uint64_t cn, const INDEX_ARRAY *pindices, const PROPTAG_ARRAY *pungroup_proptags);
EXMIDL(get_change_indices)(const char *dir, uint64_t message_id, uint64_t cn, IDLOUT INDEX_ARRAY *indices, PROPTAG_ARRAY *ungroup_proptags);
EXMIDL(mark_modified)(const char *dir, uint64_t message_id);
EXMIDL(try_mark_submit)(const char *dir, uint64_t message_id, IDLOUT BOOL *b_marked);
EXMIDL(clear_submit)(const char *dir, uint64_t message_id, BOOL b_unsent);
EXMIDL(link_message)(const char *dir, uint32_t cpid, uint64_t folder_id, uint64_t message_id, IDLOUT BOOL *b_result);
EXMIDL(unlink_message)(const char *dir, uint32_t cpid, uint64_t folder_id, uint64_t message_id);
EXMIDL(rule_new_message)(const char *dir, const char *username, const char *account, uint32_t cpid, uint64_t folder_id, uint64_t message_id);
EXMIDL(set_message_timer)(const char *dir, uint64_t message_id, uint32_t timer_id);
EXMIDL(get_message_timer)(const char *dir, uint64_t message_id, IDLOUT uint32_t **ptimer_id);
EXMIDL(empty_folder_permission)(const char *dir, uint64_t folder_id);
EXMIDL(update_folder_permission)(const char *dir, uint64_t folder_id, BOOL b_freebusy, uint16_t count, const PERMISSION_DATA *prow);
EXMIDL(empty_folder_rule)(const char *dir, uint64_t folder_id);
EXMIDL(update_folder_rule)(const char *dir, uint64_t folder_id, uint16_t count, const RULE_DATA *prow, IDLOUT BOOL *b_exceed);
EXMIDL(delivery_message)(const char *dir, const char *from_address, const char *account, uint32_t cpid, const MESSAGE_CONTENT *pmsg, const char *pdigest, IDLOUT uint32_t *result);
EXMIDL(write_message)(const char *dir, const char *account, uint32_t cpid, uint64_t folder_id, const MESSAGE_CONTENT *pmsgctnt, IDLOUT gxerr_t *e_result);
EXMIDL(read_message)(const char *dir, const char *username, uint32_t cpid, uint64_t message_id, IDLOUT MESSAGE_CONTENT **pmsgctnt);
EXMIDL(get_content_sync)(const char *dir, uint64_t folder_id, const char *username, const IDSET *pgiven, const IDSET *pseen, const IDSET *pseen_fai, const IDSET *pread, uint32_t cpid, const RESTRICTION *prestriction, BOOL b_ordered, IDLOUT uint32_t *fai_count, uint64_t *fai_total, uint32_t *normal_count, uint64_t *normal_total, EID_ARRAY *updated_mids, EID_ARRAY *chg_mids, uint64_t *last_cn, EID_ARRAY *given_mids, EID_ARRAY *deleted_mids, EID_ARRAY *nolonger_mids, EID_ARRAY *read_mids, EID_ARRAY *unread_mids, uint64_t *last_readcn);
EXMIDL(get_hierarchy_sync)(const char *dir, uint64_t folder_id, const char *username, const IDSET *pgiven, const IDSET *pseen, IDLOUT FOLDER_CHANGES *fldchgs, uint64_t *last_cn, EID_ARRAY *given_fids, EID_ARRAY *deleted_fids);
EXMIDL(allocate_ids)(const char *dir, uint32_t count, IDLOUT uint64_t *begin_eid);
EXMIDL(subscribe_notification)(const char *dir, uint16_t notificaton_type, BOOL b_whole, uint64_t folder_id, uint64_t message_id, IDLOUT uint32_t *sub_id);
EXMIDL(unsubscribe_notification)(const char *dir, uint32_t sub_id);
EXMIDL(transport_new_mail)(const char *dir, uint64_t folder_id, uint64_t message_id, uint32_t message_flags, const char *pstr_class);
EXMIDL(check_contact_address)(const char *dir, const char *paddress, IDLOUT BOOL *b_found);
EXMIDL(get_public_folder_unread_count)(const char *dir, const char *username, uint64_t folder_id, IDLOUT uint32_t *count);
EXMIDL(unload_store)(const char *dir);
#undef EXMIDL
#undef IDLOUT
