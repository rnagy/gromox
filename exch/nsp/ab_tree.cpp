// SPDX-License-Identifier: GPL-2.0-only WITH linking exception
// SPDX-FileCopyrightText: 2022 grommunio GmbH
// This file is part of Gromox.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <libHX/defs.h>
#include <libHX/string.h>
#include <gromox/ab_tree.hpp>
#include <gromox/atomic.hpp>
#include <gromox/defs.h>
#include <gromox/fileio.h>
#include <gromox/scope.hpp>
#include <gromox/util.hpp>
#include <gromox/guid.hpp>
#include <gromox/mapidefs.h>
#include <gromox/proptags.hpp>
#include "ab_tree.h"
#include <gromox/ndr_stack.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <unistd.h>
#include <csignal>
#include <fcntl.h>
#include <pthread.h> 
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <gromox/hmacmd5.hpp>
#include "../mysql_adaptor/mysql_adaptor.h"
#include "common_util.h"
#include "nsp_types.h"

#define BASE_STATUS_CONSTRUCTING			0
#define BASE_STATUS_LIVING					1
#define BASE_STATUS_DESTRUCTING				2

#define HGROWING_SIZE						100
#undef containerof
#define containerof(var, T, member) reinterpret_cast<std::conditional<std::is_const<std::remove_pointer<decltype(var)>::type>::value, std::add_const<T>::type, T>::type *>(reinterpret_cast<std::conditional<std::is_const<std::remove_pointer<decltype(var)>::type>::value, const char, char>::type *>(var) - offsetof(T, member))

/* 
	PERSON: username, real_name, title, memo, cell, tel,
			nickname, homeaddress, create_day, maildir
	ROOM: title
	EQUIPMENT: title
	MLIST: listname, list_type(int), list_privilege(int)
	DOMAIN: domainname, title, address
	GROUP: groupname, title
	CLASS: classname
*/

using namespace gromox;

struct NSAB_NODE {
	SIMPLE_TREE_NODE stree;
	int id = 0;
	uint32_t minid = 0;
	void *d_info = nullptr;
	abnode_type node_type = abnode_type::remote;
};
using AB_NODE = NSAB_NODE;

namespace {

struct sort_item {
	SIMPLE_TREE_NODE *pnode = nullptr;
	std::string str;
	inline bool operator<(const sort_item &o) const { return strcasecmp(str.c_str(), o.str.c_str()) < 0; }
};

}

static size_t g_base_size;
static int g_file_blocks, g_ab_cache_interval;
static gromox::atomic_bool g_notify_stop;
static pthread_t g_scan_id;
static char g_nsp_org_name[256];
static std::unordered_map<int, AB_BASE> g_base_hash;
static std::mutex g_base_lock, g_remote_lock;
static std::unique_ptr<LIB_BUFFER> g_file_allocator;

static decltype(mysql_adaptor_get_org_domains) *get_org_domains;
static decltype(mysql_adaptor_get_domain_info) *get_domain_info;
static decltype(mysql_adaptor_get_domain_groups) *get_domain_groups;
static decltype(mysql_adaptor_get_group_classes) *get_group_classes;
static decltype(mysql_adaptor_get_sub_classes) *get_sub_classes;
static decltype(mysql_adaptor_get_class_users) *get_class_users;
static decltype(mysql_adaptor_get_group_users) *get_group_users;
static decltype(mysql_adaptor_get_domain_users) *get_domain_users;
static decltype(mysql_adaptor_get_mlist_ids) *get_mlist_ids;

static BOOL (*get_lang)(uint32_t codepage,
	const char *tag, char *value, int len);

static void *nspab_scanwork(void *);

static uint32_t ab_tree_make_minid(minid_type type, uint32_t value)
{
	if (type == minid_type::address && value <= 0x10)
		type = minid_type::reserved;
	auto minid = static_cast<uint32_t>(type);
	minid <<= 29;
	minid |= value;
	return minid;
}

static uint32_t ab_tree_get_minid_value(uint32_t minid)
{
	if (0 == (minid & 0x80000000)) {
		return minid;
	}
	return minid & 0x1FFFFFFF;
}

uint32_t ab_tree_get_leaves_num(const SIMPLE_TREE_NODE *pnode)
{
	uint32_t count;
	
	pnode = pnode->get_child();
	if (NULL == pnode) {
		return 0;
	}
	count = 0;
	do {
		if (ab_tree_get_node_type(pnode) < abnode_type::containers)
			count ++;
	} while ((pnode = pnode->get_sibling()) != nullptr);
	return count;
}

static SINGLE_LIST_NODE* ab_tree_get_snode()
{
	return new(std::nothrow) SINGLE_LIST_NODE;
}

static void ab_tree_put_snode(SINGLE_LIST_NODE *psnode)
{
	delete psnode;
}

static AB_NODE* ab_tree_get_abnode()
{
	return new(std::nothrow) AB_NODE;
}

static void ab_tree_put_abnode(AB_NODE *pabnode)
{
	switch (pabnode->node_type) {
	case abnode_type::domain:
		delete static_cast<sql_domain *>(pabnode->d_info);
		break;
	case abnode_type::person:
	case abnode_type::room:
	case abnode_type::equipment:
	case abnode_type::mlist:
		delete static_cast<sql_user *>(pabnode->d_info);
		break;
	case abnode_type::group:
		delete static_cast<sql_group *>(pabnode->d_info);
		break;
	case abnode_type::abclass:
		delete static_cast<sql_class *>(pabnode->d_info);
		break;
	default:
		break;
	}
	delete pabnode;
}

const SIMPLE_TREE_NODE *ab_tree_minid_to_node(const AB_BASE *pbase, uint32_t minid)
{
	auto iter = pbase->phash.find(minid);
	if (iter != pbase->phash.end())
		return &iter->second->stree;
	std::lock_guard rhold(g_remote_lock);
	for (auto psnode = single_list_get_head(&pbase->remote_list); psnode != nullptr;
		psnode=single_list_get_after(&pbase->remote_list, psnode)) {
		auto stn = static_cast<const SIMPLE_TREE_NODE *>(psnode->pdata);
		auto xab = containerof(stn, AB_NODE, stree);
		if (xab->minid == minid)
			return stn;
	}
	return NULL;
}

void ab_tree_init(const char *org_name, size_t base_size,
	int cache_interval, int file_blocks)
{
	gx_strlcpy(g_nsp_org_name, org_name, arsizeof(g_nsp_org_name));
	g_base_size = base_size;
	g_ab_cache_interval = cache_interval;
	g_file_blocks = file_blocks;
	g_notify_stop = true;
}

int ab_tree_run()
{
#define E(f, s) do { \
	query_service2(s, f); \
	if ((f) == nullptr) { \
		printf("[%s]: failed to get the \"%s\" service\n", "exchange_nsp", (s)); \
		return -1; \
	} \
} while (false)

	E(get_org_domains, "get_org_domains");
	E(get_domain_info, "get_domain_info");
	E(get_domain_groups, "get_domain_groups");
	E(get_group_classes, "get_group_classes");
	E(get_sub_classes, "get_sub_classes");
	E(get_class_users, "get_class_users");
	E(get_group_users, "get_group_users");
	E(get_domain_users, "get_domain_users");
	E(get_mlist_ids, "get_mlist_ids");
	E(get_lang, "get_lang");
#undef E
	g_file_allocator = LIB_BUFFER::create(FILE_ALLOC_SIZE,
	                   g_file_blocks, TRUE);
	if (NULL == g_file_allocator) {
		printf("[exchange_nsp]: Failed to allocate file blocks\n");
		return -3;
	}
	g_notify_stop = false;
	auto ret = pthread_create(&g_scan_id, nullptr, nspab_scanwork, nullptr);
	if (ret != 0) {
		printf("[exchange_nsp]: failed to create scanning thread: %s\n", strerror(ret));
		g_notify_stop = true;
		return -4;
	}
	pthread_setname_np(g_scan_id, "nsp_abtree_scan");
	return 0;
}

static void ab_tree_destruct_tree(SIMPLE_TREE *ptree)
{
	auto proot = ptree->get_root();
	if (NULL != proot) {
		ptree->destroy_node(proot, [](SIMPLE_TREE_NODE *nd) {
			ab_tree_put_abnode(containerof(nd, AB_NODE, stree));
		});
	}
	ptree->clear();
}

void AB_BASE::unload()
{
	auto pbase = this;
	SINGLE_LIST_NODE *pnode;
	
	gal_list.clear();
	for (auto &domain : domain_list)
		ab_tree_destruct_tree(&domain.tree);
	domain_list.clear();
	while ((pnode = single_list_pop_front(&pbase->remote_list)) != nullptr) {
		auto stn = static_cast<SIMPLE_TREE_NODE *>(pnode->pdata);
		auto xab = containerof(stn, AB_NODE, stree);
		ab_tree_put_abnode(xab);
		ab_tree_put_snode(pnode);
	}
}

AB_BASE::AB_BASE()
{
	single_list_init(&remote_list);
}

domain_node::domain_node(int id) : domain_id(id)
{
	simple_tree_init(&tree);
}

domain_node::domain_node(domain_node &&o) noexcept :
	domain_id(o.domain_id), tree(std::move(o.tree))
{
	o.tree = {};
}

domain_node::~domain_node()
{
	ab_tree_destruct_tree(&tree);
}

void ab_tree_stop()
{
	if (!g_notify_stop) {
		g_notify_stop = true;
		if (!pthread_equal(g_scan_id, {})) {
			pthread_kill(g_scan_id, SIGALRM);
			pthread_join(g_scan_id, NULL);
		}
	}
	g_base_hash.clear();
	g_file_allocator.reset();
}

static BOOL ab_tree_cache_node(AB_BASE *pbase, AB_NODE *pabnode) try
{
	return pbase->phash.emplace(pabnode->minid, pabnode).second ? TRUE : false;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1551: ENOMEM\n");
	return false;
}

static BOOL ab_tree_load_user(AB_NODE *pabnode,
    sql_user &&usr, AB_BASE *pbase)
{
	switch (usr.dtypx) {
	case DT_ROOM:
		pabnode->node_type = abnode_type::room;
		break;
	case DT_EQUIPMENT:
		pabnode->node_type = abnode_type::equipment;
		break;
	default:
		pabnode->node_type = abnode_type::person;
		break;
	}
	pabnode->id = usr.id;
	pabnode->minid = ab_tree_make_minid(minid_type::address, usr.id);
	auto iter = pbase->phash.find(pabnode->minid);
	pabnode->stree.pdata = iter != pbase->phash.end() ? &iter->second->stree : nullptr;
	if (pabnode->stree.pdata == nullptr && !ab_tree_cache_node(pbase, pabnode))
		return FALSE;
	pabnode->d_info = new(std::nothrow) sql_user(std::move(usr));
	if (pabnode->d_info == nullptr)
		return false;
	return TRUE;
}

static BOOL ab_tree_load_mlist(AB_NODE *pabnode,
    sql_user &&usr, AB_BASE *pbase)
{
	pabnode->node_type = abnode_type::mlist;
	pabnode->id = usr.id;
	pabnode->minid = ab_tree_make_minid(minid_type::address, usr.id);
	auto iter = pbase->phash.find(pabnode->minid);
	pabnode->stree.pdata = iter != pbase->phash.end() ? &iter->second->stree : nullptr;
	if (pabnode->stree.pdata == nullptr && !ab_tree_cache_node(pbase, pabnode))
		return FALSE;
	pabnode->d_info = new(std::nothrow) sql_user(std::move(usr));
	if (pabnode->d_info == nullptr)
		return false;
	return TRUE;
}

static BOOL ab_tree_load_class(
	int class_id, SIMPLE_TREE *ptree,
	SIMPLE_TREE_NODE *pnode, AB_BASE *pbase)
{
	int rows;
	AB_NODE *pabnode;
	char temp_buff[1024];
	
	std::vector<sql_class> file_subclass;
	if (!get_sub_classes(class_id, file_subclass))
		return FALSE;
	for (auto &&cls : file_subclass) {
		pabnode = ab_tree_get_abnode();
		if (NULL == pabnode) {
			return FALSE;
		}
		pabnode->node_type = abnode_type::abclass;
		pabnode->id = cls.child_id;
		pabnode->minid = ab_tree_make_minid(minid_type::abclass, cls.child_id);
		if (pbase->phash.find(pabnode->minid) == pbase->phash.end() &&
		    !ab_tree_cache_node(pbase, pabnode))
			return FALSE;
		auto child_id = cls.child_id;
		pabnode->d_info = new(std::nothrow) sql_class(std::move(cls));
		if (pabnode->d_info == nullptr)
			return false;
		auto pclass = &pabnode->stree;
		ptree->add_child(pnode, pclass, SIMPLE_TREE_ADD_LAST);
		if (!ab_tree_load_class(child_id, ptree, pclass, pbase))
			return FALSE;
	}

	std::vector<sql_user> file_user;
	rows = get_class_users(class_id, file_user);
	if (-1 == rows) {
		return FALSE;
	} else if (0 == rows) {
		return TRUE;
	}
	std::vector<sort_item> parray;
	auto cl_array = make_scope_exit([&parray]() {
		for (const auto &e : parray)
			ab_tree_put_abnode(containerof(e.pnode, AB_NODE, stree));
	});
	for (auto &&usr : file_user) {
		pabnode = ab_tree_get_abnode();
		if (NULL == pabnode) {
			return false;
		}
		if (usr.dtypx == DT_DISTLIST) {
			if (!ab_tree_load_mlist(pabnode, std::move(usr), pbase)) {
				ab_tree_put_abnode(pabnode);
				return false;
			}
		} else {
			if (!ab_tree_load_user(pabnode, std::move(usr), pbase)) {
				ab_tree_put_abnode(pabnode);
				return false;
			}
		}
		ab_tree_get_display_name(&pabnode->stree, 1252, temp_buff, arsizeof(temp_buff));
		try {
			parray.push_back(sort_item{&pabnode->stree, temp_buff});
		} catch (const std::bad_alloc &) {
			fprintf(stderr, "E-1676: ENOMEM\n");
			ab_tree_put_abnode(pabnode);
			return false;
		}
	}
	std::sort(parray.begin(), parray.end());
	for (int i = 0; i < rows; ++i)
		ptree->add_child(pnode, parray[i].pnode, SIMPLE_TREE_ADD_LAST);
	cl_array.release();
	return TRUE;
}

static BOOL ab_tree_load_tree(int domain_id,
	SIMPLE_TREE *ptree, AB_BASE *pbase)
{
	int rows;
	AB_NODE *pabnode;
	sql_domain dinfo;
	
	if (!get_domain_info(domain_id, dinfo))
		return FALSE;
	pabnode = ab_tree_get_abnode();
	if (NULL == pabnode) {
		return FALSE;
	}
	pabnode->node_type = abnode_type::domain;
	pabnode->id = domain_id;
	pabnode->minid = ab_tree_make_minid(minid_type::domain, domain_id);
	if (!ab_tree_cache_node(pbase, pabnode))
		return FALSE;
	if (!utf8_check(dinfo.name.c_str()))
		utf8_filter(dinfo.name.data());
	if (!utf8_check(dinfo.title.c_str()))
		utf8_filter(dinfo.title.data());
	if (!utf8_check(dinfo.address.c_str()))
		utf8_filter(dinfo.address.data());
	pabnode->d_info = new(std::nothrow) sql_domain(std::move(dinfo));
	if (pabnode->d_info == nullptr)
		return false;
	auto pdomain = &pabnode->stree;
	ptree->set_root(pdomain);

	std::vector<sql_group> file_group;
	if (!get_domain_groups(domain_id, file_group))
		return FALSE;
	for (auto &&grp : file_group) {
		pabnode = ab_tree_get_abnode();
		if (NULL == pabnode) {
			return FALSE;
		}
		pabnode->node_type = abnode_type::group;
		pabnode->id = grp.id;
		pabnode->minid = ab_tree_make_minid(minid_type::group, grp.id);
		if (!ab_tree_cache_node(pbase, pabnode))
			return FALSE;
		auto grp_id = grp.id;
		pabnode->d_info = new(std::nothrow) sql_group(std::move(grp));
		if (pabnode->d_info == nullptr)
			return false;
		auto pgroup = &pabnode->stree;
		ptree->add_child(pdomain, pgroup, SIMPLE_TREE_ADD_LAST);
		
		std::vector<sql_class> file_class;
		if (!get_group_classes(grp_id, file_class))
			return FALSE;
		for (auto &&cls : file_class) {
			pabnode = ab_tree_get_abnode();
			if (NULL == pabnode) {
				return FALSE;
			}
			pabnode->node_type = abnode_type::abclass;
			pabnode->id = cls.child_id;
			pabnode->minid = ab_tree_make_minid(minid_type::abclass, cls.child_id);
			if (pbase->phash.find(pabnode->minid) == pbase->phash.end() &&
			    !ab_tree_cache_node(pbase, pabnode)) {
				ab_tree_put_abnode(pabnode);
				return FALSE;
			}
			pabnode->d_info = new(std::nothrow) sql_class(std::move(cls));
			if (pabnode->d_info == nullptr)
				return false;
			auto pclass = &pabnode->stree;
			ptree->add_child(pgroup, pclass, SIMPLE_TREE_ADD_LAST);
			if (!ab_tree_load_class(cls.child_id, ptree, pclass, pbase))
				return FALSE;
		}
		
		std::vector<sql_user> file_user;
		rows = get_group_users(grp_id, file_user);
		if (-1 == rows) {
			return FALSE;
		} else if (0 == rows) {
			continue;
		}
		std::vector<sort_item> parray;
		auto cl_array = make_scope_exit([&parray]() {
			for (const auto &e : parray)
				ab_tree_put_abnode(containerof(e.pnode, AB_NODE, stree));
		});
		for (auto &&usr : file_user) {
			pabnode = ab_tree_get_abnode();
			if (NULL == pabnode) {
				return false;
			}
			if (usr.dtypx == DT_DISTLIST) {
				if (!ab_tree_load_mlist(pabnode, std::move(usr), pbase)) {
					ab_tree_put_abnode(pabnode);
					return false;
				}
			} else {
				if (!ab_tree_load_user(pabnode, std::move(usr), pbase)) {
					ab_tree_put_abnode(pabnode);
					return false;
				}
			}
			char temp_buff[1024];
			ab_tree_get_display_name(&pabnode->stree, 1252, temp_buff, arsizeof(temp_buff));
			try {
				parray.push_back(sort_item{&pabnode->stree, temp_buff});
			} catch (const std::bad_alloc &) {
				fprintf(stderr, "E-1674: ENOMEM\n");
				ab_tree_put_abnode(pabnode);
				return false;
			}
		}
		std::sort(parray.begin(), parray.end());
		for (int i = 0; i < rows; ++i)
			ptree->add_child(pgroup, parray[i].pnode, SIMPLE_TREE_ADD_LAST);
		cl_array.release();
	}
	
	std::vector<sql_user> file_user;
	rows = get_domain_users(domain_id, file_user);
	if (-1 == rows) {
		return FALSE;
	} else if (0 == rows) {
		return TRUE;
	}
	std::vector<sort_item> parray;
	auto cl_array = make_scope_exit([&parray]() {
		for (const auto &e : parray)
			ab_tree_put_abnode(containerof(e.pnode, AB_NODE, stree));
	});
	for (auto &&usr : file_user) {
		pabnode = ab_tree_get_abnode();
		if (NULL == pabnode) {
			return false;
		}
		if (usr.dtypx == DT_DISTLIST) {
			if (!ab_tree_load_mlist(pabnode, std::move(usr), pbase)) {
				ab_tree_put_abnode(pabnode);
				return false;
			}
		} else {
			if (!ab_tree_load_user(pabnode, std::move(usr), pbase)) {
				ab_tree_put_abnode(pabnode);
				return false;
			}
		}
		char temp_buff[1024];
		ab_tree_get_display_name(&pabnode->stree, 1252, temp_buff, arsizeof(temp_buff));
		try {
			parray.push_back(sort_item{&pabnode->stree, temp_buff});
		} catch (const std::bad_alloc &) {
			fprintf(stderr, "E-1675: ENOMEM\n");
			ab_tree_put_abnode(pabnode);
			return false;
		}
		cl_array.release();
	}
	std::sort(parray.begin(), parray.end());
	for (int i = 0; i < rows; ++i)
		ptree->add_child(pdomain, parray[i].pnode, SIMPLE_TREE_ADD_LAST);
	cl_array.release();
	return TRUE;
}

static BOOL ab_tree_load_base(AB_BASE *pbase) try
{
	char temp_buff[1024];
	
	if (pbase->base_id > 0) {
		std::vector<int> temp_file;
		if (!get_org_domains(pbase->base_id, temp_file))
			return FALSE;
		for (auto domain_id : temp_file) {
			domain_node dnode(domain_id);
			if (!ab_tree_load_tree(dnode.domain_id, &dnode.tree, pbase))
				return FALSE;
			pbase->domain_list.push_back(std::move(dnode));
		}
	} else {
		domain_node dnode(-pbase->base_id);
		if (!ab_tree_load_tree(dnode.domain_id, &dnode.tree, pbase))
			return FALSE;
		pbase->domain_list.push_back(std::move(dnode));
	}
	for (auto &domain : pbase->domain_list) {
		auto pdomain = &domain;
		auto proot = pdomain->tree.get_root();
		if (NULL == proot) {
			continue;
		}
		simple_tree_enum_from_node(proot, [&pbase](SIMPLE_TREE_NODE *nd) {
			auto node_type = ab_tree_get_node_type(nd);
			if (node_type >= abnode_type::containers || nd->pdata != nullptr)
				return;
			pbase->gal_list.push_back(nd);
		});
	}
	if (pbase->gal_list.size() <= 1)
		return TRUE;
	std::vector<sort_item> parray;
	for (auto ptr : pbase->gal_list) {
		ab_tree_get_display_name(ptr, 1252, temp_buff, arsizeof(temp_buff));
		parray.push_back(sort_item{ptr, temp_buff});
	}
	std::sort(parray.begin(), parray.end());
	size_t i = 0;
	for (auto &ptr : pbase->gal_list)
		ptr = parray[i++].pnode;
	return TRUE;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1677: ENOMEM\n");
	return TRUE;
}

AB_BASE_REF ab_tree_get_base(int base_id)
{
	int count;
	AB_BASE *pbase;
	
	count = 0;
 RETRY_LOAD_BASE:
	std::unique_lock bhold(g_base_lock);
	auto it = g_base_hash.find(base_id);
	if (it == g_base_hash.end()) {
		if (g_base_hash.size() >= g_base_size) {
			printf("[exchange_nsp]: W-1298: AB base hash is full\n");
			return nullptr;
		}
		try {
			auto xp = g_base_hash.try_emplace(base_id);
			if (!xp.second)
				return nullptr;
			it = xp.first;
			pbase = &xp.first->second;
		} catch (const std::bad_alloc &) {
			return nullptr;
		}
		pbase->base_id = base_id;
		pbase->status = BASE_STATUS_CONSTRUCTING;
		pbase->guid = guid_random_new();
		memcpy(pbase->guid.node, &base_id, sizeof(uint32_t));
		pbase->phash.clear();
		bhold.unlock();
		if (!ab_tree_load_base(pbase)) {
			pbase->unload();
			bhold.lock();
			g_base_hash.erase(it);
			bhold.unlock();
			return nullptr;
		}
		time(&pbase->load_time);
		bhold.lock();
		pbase->status = BASE_STATUS_LIVING;
	} else {
		pbase = &it->second;
		if (pbase->status != BASE_STATUS_LIVING) {
			bhold.unlock();
			count ++;
			if (count > 60) {
				return nullptr;
			}
			sleep(1);
			goto RETRY_LOAD_BASE;
		}
	}
	pbase->reference ++;
	return AB_BASE_REF(pbase);
}

void ab_tree_del::operator()(AB_BASE *pbase)
{
	std::lock_guard bhold(g_base_lock);
	pbase->reference --;
}

static void *nspab_scanwork(void *param)
{
	AB_BASE *pbase;
	SINGLE_LIST_NODE *pnode;
	
	while (!g_notify_stop) {
		pbase = NULL;
		std::unique_lock bhold(g_base_lock);
		for (auto &kvpair : g_base_hash) {
			auto &base = kvpair.second;
			if (base.status != BASE_STATUS_LIVING ||
			    base.reference != 0 ||
			    time(nullptr) - base.load_time < g_ab_cache_interval)
				continue;
			pbase = &base;
			pbase->status = BASE_STATUS_CONSTRUCTING;
			break;
		}
		bhold.unlock();
		if (NULL == pbase) {
			sleep(1);
			continue;
		}
		pbase->gal_list.clear();
		for (auto &domain : pbase->domain_list)
			ab_tree_destruct_tree(&domain.tree);
		pbase->domain_list.clear();
		while ((pnode = single_list_pop_front(&pbase->remote_list)) != nullptr) {
			auto stn = static_cast<SIMPLE_TREE_NODE *>(pnode->pdata);
			auto xab = containerof(stn, AB_NODE, stree);
			ab_tree_put_abnode(xab);
			ab_tree_put_snode(pnode);
		}
		pbase->phash.clear();
		if (!ab_tree_load_base(pbase)) {
			pbase->unload();
			bhold.lock();
			g_base_hash.erase(pbase->base_id);
			bhold.unlock();
		} else {
			bhold.lock();
			time(&pbase->load_time);
			pbase->status = BASE_STATUS_LIVING;
			bhold.unlock();
		}
	}
	return NULL;
}

static int ab_tree_node_to_rpath(const SIMPLE_TREE_NODE *pnode,
	char *pbuff, int length)
{
	auto pabnode = containerof(pnode, AB_NODE, stree);
	char k;
	
	switch (pabnode->node_type) {
	case abnode_type::domain: k = 'd'; break;
	case abnode_type::group: k = 'g'; break;
	case abnode_type::abclass: k = 'c'; break;
	case abnode_type::person: k = 'p'; break;
	case abnode_type::mlist: k = 'l'; break;
	case abnode_type::room: k = 'r'; break;
	case abnode_type::equipment: k = 'e'; break;
	default: return 0;
	}
	char temp_buff[HXSIZEOF_Z32+2];
	auto len = sprintf(temp_buff, "%c%d", k, pabnode->id);
	if (len >= length) {
		return 0;
	}
	memcpy(pbuff, temp_buff, len + 1);
	return len;
}

static BOOL ab_tree_node_to_path(const SIMPLE_TREE_NODE *pnode,
	char *pbuff, int length)
{
	int len;
	int offset;
	AB_BASE_REF pbase;
	auto xab = containerof(pnode, AB_NODE, stree);
	
	if (xab->node_type == abnode_type::remote) {
		pbase = ab_tree_get_base(-xab->id);
		if (pbase == nullptr)
			return FALSE;
		auto iter = pbase->phash.find(xab->minid);
		if (iter == pbase->phash.end())
			return FALSE;
		xab = iter->second;
		pnode = &xab->stree;
	}
	
	offset = 0;
	do {
		len = ab_tree_node_to_rpath(pnode,
			pbuff + offset, length - offset);
		if (0 == len) {
			return FALSE;
		}
		offset += len;
	} while ((pnode = pnode->get_parent()) != nullptr);
	return TRUE;
}


static bool ab_tree_md5_path(const char *path, uint64_t *pdgt) __attribute__((warn_unused_result));
static bool ab_tree_md5_path(const char *path, uint64_t *pdgt)
{
	int i;
	uint64_t b;
	uint8_t dgt_buff[MD5_DIGEST_LENGTH];
	std::unique_ptr<EVP_MD_CTX, sslfree> ctx(EVP_MD_CTX_new());

	if (ctx == nullptr ||
	    EVP_DigestInit(ctx.get(), EVP_md5()) <= 0 ||
	    EVP_DigestUpdate(ctx.get(), path, strlen(path)) <= 0 ||
	    EVP_DigestFinal(ctx.get(), dgt_buff, nullptr) <= 0)
		return false;
	*pdgt = 0;
	for (i=0; i<16; i+=2) {
		b = dgt_buff[i];
		*pdgt |= (b << 4*i);
	}
	return true;
}

bool ab_tree_node_to_guid(const SIMPLE_TREE_NODE *pnode, GUID *pguid)
{
	uint64_t dgt;
	uint32_t tmp_id;
	char temp_path[512];
	const SIMPLE_TREE_NODE *proot;
	auto pabnode = containerof(pnode, AB_NODE, stree);
	
	if (pabnode->node_type < abnode_type::containers &&
	    pnode->pdata != nullptr)
		return ab_tree_node_to_guid(static_cast<const SIMPLE_TREE_NODE *>(pnode->pdata), pguid);
	memset(pguid, 0, sizeof(GUID));
	pguid->time_low = static_cast<unsigned int>(pabnode->node_type) << 24;
	if (pabnode->node_type == abnode_type::remote) {
		pguid->time_low |= pabnode->id;
		tmp_id = ab_tree_get_minid_value(pabnode->minid);
		pguid->time_hi_and_version = (tmp_id & 0xFFFF0000) >> 16;
		pguid->time_mid = tmp_id & 0xFFFF;
	} else {
		proot = pnode;
		const SIMPLE_TREE_NODE *pnode1;
		while ((pnode1 = proot->get_parent()) != nullptr)
			proot = pnode1;
		auto abroot = containerof(proot, AB_NODE, stree);
		pguid->time_low |= abroot->id;
		pguid->time_hi_and_version = (pabnode->id & 0xFFFF0000) >> 16;
		pguid->time_mid = pabnode->id & 0xFFFF;
	}
	memset(temp_path, 0, sizeof(temp_path));
	ab_tree_node_to_path(&pabnode->stree, temp_path, arsizeof(temp_path));
	if (!ab_tree_md5_path(temp_path, &dgt))
		return false;
	pguid->node[0] = dgt & 0xFF;
	pguid->node[1] = (dgt & 0xFF00) >> 8;
	pguid->node[2] = (dgt & 0xFF0000) >> 16;
	pguid->node[3] = (dgt & 0xFF000000) >> 24;
	pguid->node[4] = (dgt & 0xFF00000000ULL) >> 32;
	pguid->node[5] = (dgt & 0xFF0000000000ULL) >> 40;
	pguid->clock_seq[0] = (dgt & 0xFF000000000000ULL) >> 48;
	pguid->clock_seq[1] = (dgt & 0xFF00000000000000ULL) >> 56;
	return true;
}

BOOL ab_tree_node_to_dn(const SIMPLE_TREE_NODE *pnode, char *pbuff, int length)
{
	int id;
	char *ptoken;
	int domain_id;
	AB_BASE_REF pbase;
	char cusername[UADDR_SIZE];
	char hex_string[32];
	char hex_string1[32];
	auto pabnode = containerof(pnode, AB_NODE, stree);
	
	if (pabnode->node_type == abnode_type::remote) {
		pbase = ab_tree_get_base(-pabnode->id);
		if (pbase == nullptr)
			return FALSE;
		auto iter = pbase->phash.find(pabnode->minid);
		if (iter == pbase->phash.end())
			return FALSE;
		pabnode = iter->second;
		pnode = &pabnode->stree;
	}
	switch (pabnode->node_type) {
	case abnode_type::person:
	case abnode_type::room:
	case abnode_type::equipment:
		id = pabnode->id;
		ab_tree_get_user_info(pnode, USER_MAIL_ADDRESS, cusername, arsizeof(cusername));
		ptoken = strchr(cusername, '@');
		if (NULL != ptoken) {
			*ptoken = '\0';
		}
		while ((pnode = pnode->get_parent()) != nullptr)
			pabnode = containerof(pnode, AB_NODE, stree);
		if (pabnode->node_type != abnode_type::domain)
			return FALSE;
		domain_id = pabnode->id;
		encode_hex_int(id, hex_string);
		encode_hex_int(domain_id, hex_string1);
		sprintf(pbuff, "/o=%s/ou=Exchange Administrative Group"
				" (FYDIBOHF23SPDLT)/cn=Recipients/cn=%s%s-%s",
			g_nsp_org_name, hex_string1, hex_string, cusername);
		HX_strupper(pbuff);
		break;
	case abnode_type::mlist: try {
		id = pabnode->id;
		auto obj = static_cast<sql_user *>(pabnode->d_info);
		std::string username = obj->username;
		auto pos = username.find('@');
		if (pos != username.npos)
			username.erase(pos);
		while ((pnode = pnode->get_parent()) != nullptr)
			pabnode = containerof(pnode, AB_NODE, stree);
		if (pabnode->node_type != abnode_type::domain)
			return FALSE;
		domain_id = pabnode->id;
		encode_hex_int(id, hex_string);
		encode_hex_int(domain_id, hex_string1);
		sprintf(pbuff, "/o=%s/ou=Exchange Administrative Group"
				" (FYDIBOHF23SPDLT)/cn=Recipients/cn=%s%s-%s",
			g_nsp_org_name, hex_string1, hex_string, username.c_str());
		HX_strupper(pbuff);
		break;
	} catch (...) {
		return false;
	}
	default:
		return FALSE;
	}
	return TRUE;	
}

const SIMPLE_TREE_NODE *ab_tree_dn_to_node(AB_BASE *pbase, const char *pdn)
{
	int id;
	int temp_len;
	int domain_id;
	AB_NODE *pabnode;
	char prefix_string[1024];
	
	temp_len = gx_snprintf(prefix_string, GX_ARRAY_SIZE(prefix_string), "/o=%s/ou=Exchange "
			"Administrative Group (FYDIBOHF23SPDLT)", g_nsp_org_name);
	if (temp_len < 0 || strncasecmp(pdn, prefix_string, temp_len) != 0)
		return NULL;
	if (strncasecmp(pdn + temp_len, "/cn=Configuration/cn=Servers/cn=", 32) == 0 &&
	    strlen(pdn) >= static_cast<size_t>(temp_len) + 60) {
		/* Reason for 60: see DN format in ab_tree_get_server_dn */
		id = decode_hex_int(pdn + temp_len + 60);
		auto minid = ab_tree_make_minid(minid_type::address, id);
		auto iter = pbase->phash.find(minid);
		return iter != pbase->phash.end() ? &iter->second->stree : nullptr;
	}
	if (0 != strncasecmp(pdn + temp_len, "/cn=Recipients/cn=", 18)) {
		return NULL;
	}
	domain_id = decode_hex_int(pdn + temp_len + 18);
	id = decode_hex_int(pdn + temp_len + 26);
	auto minid = ab_tree_make_minid(minid_type::address, id);
	auto iter = pbase->phash.find(minid);
	if (iter != pbase->phash.end())
		return &iter->second->stree;
	std::unique_lock rhold(g_remote_lock);
	for (auto psnode = single_list_get_head(&pbase->remote_list); psnode != nullptr;
		psnode=single_list_get_after(&pbase->remote_list, psnode)) {
		auto stn = static_cast<const SIMPLE_TREE_NODE *>(psnode->pdata);
		auto xab = containerof(stn, AB_NODE, stree);
		if (xab->minid == minid)
			return stn;
	}
	rhold.unlock();
	for (auto &domain : pbase->domain_list)
		if (domain.domain_id == domain_id)
			return NULL;
	auto pbase1 = ab_tree_get_base(-domain_id);
	if (pbase1 == nullptr)
		return NULL;
	iter = pbase1->phash.find(minid);
	if (iter == pbase1->phash.end())
		return NULL;
	auto xab = iter->second;
	auto psnode = ab_tree_get_snode();
	if (NULL == psnode) {
		return NULL;
	}
	pabnode = ab_tree_get_abnode();
	if (NULL == pabnode) {
		ab_tree_put_snode(psnode);
		return NULL;
	}
	psnode->pdata = pabnode;
	pabnode->stree.pdata = nullptr;
	pabnode->node_type = abnode_type::remote;
	pabnode->minid = xab->minid;
	pabnode->id = domain_id;
	pabnode->d_info = nullptr;
	if (xab->node_type == abnode_type::remote)
		fprintf(stderr, "W-1568: unexplored case\n");
	else if (xab->node_type == abnode_type::domain)
		pabnode->d_info = new(std::nothrow) sql_domain(*static_cast<sql_domain *>(xab->d_info));
	else if (xab->node_type == abnode_type::group)
		pabnode->d_info = new(std::nothrow) sql_group(*static_cast<sql_group *>(xab->d_info));
	else if (xab->node_type == abnode_type::abclass)
		pabnode->d_info = new(std::nothrow) sql_class(*static_cast<sql_class *>(xab->d_info));
	else
		pabnode->d_info = new(std::nothrow) sql_user(*static_cast<sql_user *>(xab->d_info));
	if (pabnode->d_info == nullptr && xab->node_type != abnode_type::remote) {
		ab_tree_put_abnode(pabnode);
		ab_tree_put_snode(psnode);
		return nullptr;
	}
	pbase1.reset();
	rhold.lock();
	single_list_append_as_tail(&pbase->remote_list, psnode);
	return &pabnode->stree;
}

const SIMPLE_TREE_NODE *ab_tree_uid_to_node(const AB_BASE *pbase, int user_id)
{
	auto minid = ab_tree_make_minid(minid_type::address, user_id);
	auto iter = pbase->phash.find(minid);
	return iter != pbase->phash.end() ? &iter->second->stree : nullptr;
}

uint32_t ab_tree_get_node_minid(const SIMPLE_TREE_NODE *pnode)
{
	const AB_NODE *xab = containerof(const_cast<SIMPLE_TREE_NODE *>(pnode), AB_NODE, stree);
	return xab->minid;
}

abnode_type ab_tree_get_node_type(const SIMPLE_TREE_NODE *pnode)
{
	auto pabnode = containerof(pnode, AB_NODE, stree);
	if (pabnode->node_type != abnode_type::remote)
		return pabnode->node_type;
	auto pbase = ab_tree_get_base(-pabnode->id);
	if (pbase == nullptr)
		return abnode_type::remote;
	auto iter = pbase->phash.find(pabnode->minid);
	if (iter == pbase->phash.end())
		return abnode_type::remote;
	return iter->second->node_type;
}

void ab_tree_get_display_name(const SIMPLE_TREE_NODE *pnode, uint32_t codepage,
    char *str_dname, size_t dn_size)
{
	char *ptoken;
	char lang_string[256];
	
	auto pabnode = containerof(pnode, AB_NODE, stree);
	if (dn_size > 0)
		str_dname[0] = '\0';
	switch (pabnode->node_type) {
	case abnode_type::domain: {
		auto obj = static_cast<sql_domain *>(pabnode->d_info);
		gx_strlcpy(str_dname, obj->title.c_str(), dn_size);
		break;
	}
	case abnode_type::group: {
		auto obj = static_cast<sql_group *>(pabnode->d_info);
		gx_strlcpy(str_dname, obj->title.c_str(), dn_size);
		break;
	}
	case abnode_type::abclass: {
		auto obj = static_cast<sql_class *>(pabnode->d_info);
		gx_strlcpy(str_dname, obj->name.c_str(), dn_size);
		break;
	}
	case abnode_type::person:
	case abnode_type::room:
	case abnode_type::equipment: {
		auto obj = static_cast<sql_user *>(pabnode->d_info);
		auto it = obj->propvals.find(PR_DISPLAY_NAME);
		if (it != obj->propvals.cend()) {
			gx_strlcpy(str_dname, it->second.c_str(), dn_size);
		} else {
			gx_strlcpy(str_dname, obj->username.c_str(), dn_size);
			ptoken = strchr(str_dname, '@');
			if (NULL != ptoken) {
				*ptoken = '\0';
			}
		}
		break;
	}
	case abnode_type::mlist: {
		auto obj = static_cast<sql_user *>(pabnode->d_info);
		auto it = obj->propvals.find(PR_DISPLAY_NAME);
		switch (obj->list_type) {
		case MLIST_TYPE_NORMAL:
			if (!get_lang(codepage, "mlist0", lang_string,
			    arsizeof(lang_string)))
				strcpy(lang_string, "custom address list");
			snprintf(str_dname, dn_size, "%s(%s)", obj->username.c_str(), lang_string);
			break;
		case MLIST_TYPE_GROUP:
			if (!get_lang(codepage, "mlist1", lang_string,
			    arsizeof(lang_string)))
				strcpy(lang_string, "all users in department of %s");
			snprintf(str_dname, dn_size, lang_string, it != obj->propvals.cend() ? it->second.c_str() : "");
			break;
		case MLIST_TYPE_DOMAIN:
			if (!get_lang(codepage, "mlist2", str_dname, dn_size))
				gx_strlcpy(str_dname, "all users in domain", dn_size);
			break;
		case MLIST_TYPE_CLASS:
			if (!get_lang(codepage, "mlist3", lang_string,
			    arsizeof(lang_string)))
				strcpy(lang_string, "all users in group of %s");
			snprintf(str_dname, dn_size, lang_string, it != obj->propvals.cend() ? it->second.c_str() : "");
			break;
		default:
			snprintf(str_dname, dn_size, "unknown address list type %u", obj->list_type);
		}
		break;
	}
	default:
		break;
	}
}

std::vector<std::string>
ab_tree_get_object_aliases(const SIMPLE_TREE_NODE *pnode)
{
	std::vector<std::string> alist;
	auto pabnode = containerof(pnode, AB_NODE, stree);
	for (const auto &a : static_cast<sql_user *>(pabnode->d_info)->aliases)
		alist.push_back(a);
	return alist;
}

void ab_tree_get_user_info(const SIMPLE_TREE_NODE *pnode, int type,
    char *value, size_t vsize)
{
	value[0] = '\0';
	auto pabnode = containerof(pnode, AB_NODE, stree);
	if (pabnode->node_type != abnode_type::person &&
	    pabnode->node_type != abnode_type::room &&
	    pabnode->node_type != abnode_type::equipment &&
	    pabnode->node_type != abnode_type::remote)
		return;
	auto u = static_cast<sql_user *>(pabnode->d_info);
	unsigned int tag = 0;
	switch (type) {
	case USER_MAIL_ADDRESS: gx_strlcpy(value, u->username.c_str(), vsize); return;
	case USER_REAL_NAME: tag = PR_DISPLAY_NAME; break;
	case USER_JOB_TITLE: tag = PR_TITLE; break;
	case USER_COMMENT: tag = PR_COMMENT; break;
	case USER_MOBILE_TEL: tag = PR_MOBILE_TELEPHONE_NUMBER; break;
	case USER_BUSINESS_TEL: tag = PR_PRIMARY_TELEPHONE_NUMBER; break;
	case USER_NICK_NAME: tag = PR_NICKNAME; break;
	case USER_HOME_ADDRESS: tag = PR_HOME_ADDRESS_STREET; break;
	case USER_CREATE_DAY: *value = '\0'; return;
	case USER_STORE_PATH: strcpy(value, u->maildir.c_str()); return;
	}
	if (tag == 0)
		return;
	auto it = u->propvals.find(tag);
	if (it != u->propvals.cend())
		gx_strlcpy(value, it->second.c_str(), vsize);
}

void ab_tree_get_mlist_info(const SIMPLE_TREE_NODE *pnode,
	char *mail_address, char *create_day, int *plist_privilege)
{
	auto pabnode = containerof(pnode, AB_NODE, stree);
	if (pabnode->node_type != abnode_type::mlist &&
	    pabnode->node_type != abnode_type::remote) {
		mail_address[0] = '\0';
		*plist_privilege = 0;
		return;
	}
	auto obj = static_cast<sql_user *>(pabnode->d_info);
	if (mail_address != nullptr)
		strcpy(mail_address, obj->username.c_str());
	if (create_day != nullptr)
		*create_day = '\0';
	if (plist_privilege != nullptr)
		*plist_privilege = obj->list_priv;
}

void ab_tree_get_mlist_title(uint32_t codepage, char *str_title)
{
	if (!get_lang(codepage, "mlist", str_title, 256))
		strcpy(str_title, "Address List");
}

void ab_tree_get_server_dn(const SIMPLE_TREE_NODE *pnode, char *dn, int length)
{
	char *ptoken;
	char username[UADDR_SIZE];
	char hex_string[32];
	
	auto xab = containerof(pnode, AB_NODE, stree);
	if (xab->node_type >= abnode_type::containers)
		return;
	memset(username, 0, sizeof(username));
	ab_tree_get_user_info(pnode, USER_MAIL_ADDRESS, username, GX_ARRAY_SIZE(username));
	ptoken = strchr(username, '@');
	HX_strlower(username);
	if (NULL != ptoken) {
		ptoken++;
	} else {
		ptoken = username;
	}
	if (xab->node_type == abnode_type::remote)
		encode_hex_int(ab_tree_get_minid_value(xab->minid), hex_string);
	else
		encode_hex_int(xab->id, hex_string);
	snprintf(dn, length, "/o=%s/ou=Exchange Administrative "
	         "Group (FYDIBOHF23SPDLT)/cn=Configuration/cn=Servers"
	         "/cn=%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x"
	         "-%02x%02x%s@%s", g_nsp_org_name, username[0], username[1],
	         username[2], username[3], username[4], username[5],
	         username[6], username[7], username[8], username[9],
	         username[10], username[11], hex_string, ptoken);
	HX_strupper(dn);
}

void ab_tree_get_company_info(const SIMPLE_TREE_NODE *pnode,
	char *str_name, char *str_address)
{
	AB_BASE_REF pbase;
	auto pabnode = containerof(pnode, AB_NODE, stree);
	
	if (pabnode->node_type == abnode_type::remote) {
		pbase = ab_tree_get_base(-pabnode->id);
		if (pbase == nullptr) {
			str_name[0] = '\0';
			str_address[0] = '\0';
			return;
		}
		auto iter = pbase->phash.find(pabnode->minid);
		if (iter == pbase->phash.end()) {
			str_name[0] = '\0';
			str_address[0] = '\0';
			return;
		}
		pabnode = iter->second;
		pnode = &pabnode->stree;
	}
	while ((pnode = pnode->get_parent()) != nullptr)
		pabnode = containerof(pnode, AB_NODE, stree);
	auto obj = static_cast<sql_domain *>(pabnode->d_info);
	if (str_name != nullptr)
		strcpy(str_name, obj->title.c_str());
	if (str_address != nullptr)
		strcpy(str_address, obj->address.c_str());
}

void ab_tree_get_department_name(const SIMPLE_TREE_NODE *pnode, char *str_name)
{
	AB_BASE_REF pbase;
	auto pabnode = containerof(pnode, AB_NODE, stree);
	
	if (pabnode->node_type == abnode_type::remote) {
		pbase = ab_tree_get_base(-pabnode->id);
		if (pbase == nullptr) {
			str_name[0] = '\0';
			return;
		}
		auto iter = pbase->phash.find(pabnode->minid);
		if (iter == pbase->phash.end()) {
			str_name[0] = '\0';
			return;
		}
		pabnode = iter->second;
		pnode = &pabnode->stree;
	}
	do {
		pabnode = containerof(pnode, AB_NODE, stree);
		if (pabnode->node_type == abnode_type::group)
			break;
	} while ((pnode = pnode->get_parent()) != nullptr);
	if (NULL == pnode) {
		str_name[0] = '\0';
		return;
	}
	auto obj = static_cast<sql_group *>(pabnode->d_info);
	strcpy(str_name, obj->title.c_str());
}

int ab_tree_get_guid_base_id(GUID guid)
{
	int32_t base_id;
	
	memcpy(&base_id, guid.node, sizeof(int32_t));
	std::lock_guard bhold(g_base_lock);
	return g_base_hash.find(base_id) != g_base_hash.end() ? base_id : 0;
}

ec_error_t ab_tree_fetchprop(const SIMPLE_TREE_NODE *node, unsigned int codepage,
    unsigned int proptag, PROPERTY_VALUE *prop)
{
	auto node_type = ab_tree_get_node_type(node);
	if (node_type != abnode_type::person && node_type != abnode_type::room &&
	    node_type != abnode_type::equipment && node_type != abnode_type::mlist)
		return ecNotFound;
	auto xab = containerof(node, AB_NODE, stree);
	const auto &obj = *static_cast<sql_user *>(xab->d_info);
	auto it = obj.propvals.find(proptag);
	if (it == obj.propvals.cend())
		return ecNotFound;

	switch (PROP_TYPE(proptag)) {
	case PT_BOOLEAN:
		prop->value.b = strtol(it->second.c_str(), nullptr, 0) != 0;
		return ecSuccess;
	case PT_SHORT:
		prop->value.s = strtol(it->second.c_str(), nullptr, 0);
		return ecSuccess;
	case PT_LONG:
		prop->value.l = strtol(it->second.c_str(), nullptr, 0);
		return ecSuccess;
	case PT_I8:
		prop->value.l = strtoll(it->second.c_str(), nullptr, 0);
		return ecSuccess;
	case PT_SYSTIME:
		common_util_day_to_filetime(it->second.c_str(), &prop->value.ftime);
		return ecSuccess;
	case PT_STRING8: {
		auto tg = ndr_stack_anew<char>(NDR_STACK_OUT, it->second.size() + 1);
		if (tg == nullptr)
			return ecMAPIOOM;
		auto ret = common_util_from_utf8(codepage, it->second.c_str(), tg, it->second.size());
		if (ret < 0)
			return ecError;
		tg[ret] = '\0';
		prop->value.pstr = tg;
		return ecSuccess;
	}
	case PT_UNICODE: {
		auto tg = ndr_stack_anew<char>(NDR_STACK_OUT, it->second.size() + 1);
		if (tg == nullptr)
			return ecMAPIOOM;
		strcpy(tg, it->second.c_str());
		prop->value.pstr = tg;
		return ecSuccess;
	}
	case PT_BINARY: {
		prop->value.bin.cb = it->second.size();
		prop->value.bin.pv = ndr_stack_alloc(NDR_STACK_OUT, it->second.size());
		if (prop->value.bin.pv == nullptr)
			return ecMAPIOOM;
		memcpy(prop->value.bin.pv, it->second.data(), prop->value.bin.cb);
		return ecSuccess;
	}
	case PT_MV_UNICODE: {
		auto &x = prop->value.string_array;
		x.count = 1;
		x.ppstr = ndr_stack_anew<char *>(NDR_STACK_OUT);
		if (x.ppstr == nullptr)
			return ecMAPIOOM;
		auto tg = ndr_stack_anew<char>(NDR_STACK_OUT, it->second.size() + 1);
		if (tg == nullptr)
			return ecMAPIOOM;
		strcpy(tg, it->second.c_str());
		x.ppstr[0] = tg;
		return ecSuccess;
	}
	}
	return ecNotFound;
}

void ab_tree_invalidate_cache()
{
	printf("[exchange_nsp]: Invalidating AB caches\n");
	std::unique_lock bl_hold(g_base_lock);
	for (auto &kvpair : g_base_hash)
		kvpair.second.load_time = 0;
}
