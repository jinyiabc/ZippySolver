// Evaluate the EV to P1 at a node through Monte Carlo simulation (i.e., "play").
//
// In each "duplicate" hand, we play N hands where strategy B is assigned to each of the N
// positions one-by-one.
//
// If I want to support asymmetric systems again, I may need to go back to having a separate
// CFRValues object for each position.

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "canonical.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "params.h"
#include "sorting.h"
#include "rand48.h"

using std::string;
using std::unique_ptr;

class Player {
public:
  Player(const BettingAbstraction &a_ba, const BettingAbstraction &b_ba,
	 const CardAbstraction &a_ca, const CardAbstraction &b_ca, const CFRConfig &a_cc,
	 const CFRConfig &b_cc, int a_it, int b_it);
  ~Player(void);
  void Go(long long int num_duplicate_hands, const string &target_action_sequence);
private:
  void DealNCards(Card *cards, int n);
  void SetHCPsAndBoards(Card **raw_hole_cards, const Card *raw_board);
  void Play(Node **nodes, int b_pos, int *contributions, int last_bet_to, bool *folded,
	    int num_remaining, int last_player_acting, const string &action_sequence,
	    const string &target_action_sequence, int last_st);
  void PlayDuplicateHand(unsigned long long int h, const Card *cards,
			 const string &target_action_sequence);

  int num_players_;
  bool a_asymmetric_;
  bool b_asymmetric_;
  BettingTree **a_betting_trees_;
  BettingTree **b_betting_trees_;
  const Buckets *a_buckets_;
  const Buckets *b_buckets_;
  unique_ptr<CFRValues> a_probs_;
  unique_ptr<CFRValues> b_probs_;
  int *boards_;
  int **raw_hcps_;
  unique_ptr<int []> hvs_;
  unique_ptr<bool []> winners_;
  unsigned short **sorted_hcps_;
  struct drand48_data rand_buf_;
  double sum_target_p1_outcomes_;
  long long int num_target_p1_outcomes_;
};

void Player::Play(Node **nodes, int b_pos, int *contributions, int last_bet_to, bool *folded,
		  int num_remaining, int last_player_acting, const string &action_sequence,
		  const string &target_action_sequence, int last_st) {
  if (action_sequence == target_action_sequence) {
    ++num_target_p1_outcomes_;
  }
  Node *p0_node = nodes[0];
  if (p0_node->Terminal()) {
    double p1_outcome;
    if (num_remaining == 1) {
      // Assume two player
      if (folded[1]) {
	p1_outcome = -contributions[1];
      } else {
	p1_outcome = contributions[0];
      }
    } else {
      // Showdown
      // Temporary?
      if (num_players_ == 2 && (contributions[0] != contributions[1] ||
				contributions[0] != p0_node->LastBetTo())) {
	fprintf(stderr, "Mismatch %i %i %i\n", contributions[0], contributions[1],
		p0_node->LastBetTo());
	fprintf(stderr, "TID: %u\n", p0_node->TerminalID());
	exit(-1);
      }

      // Find the best hand value of anyone remaining in the hand, and the
      // total pot size which includes contributions from remaining players
      // and players who folded earlier.
      int best_hv = 0;
      int pot_size = 0;
      for (int p = 0; p < num_players_; ++p) {
	pot_size += contributions[p];
	if (! folded[p]) {
	  int hv = hvs_[p];
	  if (hv > best_hv) best_hv = hv;
	}
      }

      // Determine if we won, the number of winners, and the total contribution
      // of all winners.
      int num_winners = 0;
      int winner_contributions = 0;
      for (int p = 0; p < num_players_; ++p) {
	if (! folded[p] && hvs_[p] == best_hv) {
	  winners_[p] = true;
	  ++num_winners;
	  winner_contributions += contributions[p];
	} else {
	  winners_[p] = false;
	}
      }

      // Assume two players
      if (winners_[1]) {
	p1_outcome = ((double)(pot_size - winner_contributions)) / ((double)num_winners);
      } else {
	p1_outcome = -contributions[1];
      }
    }
    if (action_sequence == target_action_sequence) {
      sum_target_p1_outcomes_ += p1_outcome;
    }
    return;
  } else {
#if 0
    if (nodes[0]->NonterminalID() != nodes[1]->NonterminalID()) {
      fprintf(stderr, "NT ID mismatch %i vs %i st %i vs %i lbt %i vs %i pa %i vs %i\n",
	      nodes[0]->NonterminalID(), nodes[1]->NonterminalID(), nodes[0]->Street(),
	      nodes[1]->Street(), nodes[0]->LastBetTo(), nodes[1]->LastBetTo(),
	      nodes[0]->PlayerActing(), nodes[1]->PlayerActing());
      exit(-1);
    }
#endif
    // Assumption is that we can get the street from any node
    int st = p0_node->Street();
    // Assumption is that we can get num_succs from any node
    // Won't work for asymmetric maybe
    int num_succs = p0_node->NumSuccs();
    // Find the next player to act.  Start with the first candidate and move
    // forward until we find someone who has not folded.  The first candidate
    // is either the last player plus one, or, if we are starting a new
    // betting round, the first player to act on that street.
    int actual_pa;
    if (st > last_st) actual_pa = Game::FirstToAct(st);
    else              actual_pa = last_player_acting + 1;
    while (true) {
      if (actual_pa == num_players_) actual_pa = 0;
      if (! folded[actual_pa]) break;
      ++actual_pa;
    }
    
    // Assumption is that we can get the default succ index from any node
    // Won't work for asymmetric maybe
    int dsi = p0_node->DefaultSuccIndex();
    int bd = boards_[st];
    int raw_hcp = raw_hcps_[actual_pa][st];
    int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    int a_offset, b_offset;
    // If card abstraction, hcp on river should be raw.  If no card
    // abstraction, hcp on river should be sorted.  Right?
    if (a_buckets_->None(st)) {
      int hcp = st == Game::MaxStreet() ? sorted_hcps_[bd][raw_hcp] : raw_hcp;
      a_offset = bd * num_hole_card_pairs * num_succs + hcp * num_succs;
    } else {
      unsigned int h = ((unsigned int)bd) * ((unsigned int)num_hole_card_pairs) + raw_hcp;
      int b = a_buckets_->Bucket(st, h);
      a_offset = b * num_succs;
    }
    if (b_buckets_->None(st)) {
      // Don't support full hold'em with no buckets.  Would get int overflow if we tried to do that.
      int hcp = st == Game::MaxStreet() ? sorted_hcps_[bd][raw_hcp] : raw_hcp;
      b_offset = bd * num_hole_card_pairs * num_succs + hcp * num_succs;
    } else {
      unsigned int h = ((unsigned int)bd) * ((unsigned int)num_hole_card_pairs) + raw_hcp;
      int b = b_buckets_->Bucket(st, h);
      b_offset = b * num_succs;
    }
    double r;
    // r = RandZeroToOne();
    drand48_r(&rand_buf_, &r);
    
    double cum = 0;
    unique_ptr<double []> probs(new double[num_succs]);
    // The *actual* player acting may be different from the player acting value of the current
    // node because of reentrant betting trees.  We need the *actual* player acting to determine
    // whether A or B is acting here.  Be need the node's player acting value to query the
    // probabilities for the appropriate information set.
    int nt = nodes[actual_pa]->NonterminalID();
    int node_pa = nodes[actual_pa]->PlayerActing();
    if (actual_pa == b_pos) {
      b_probs_->RMProbs(st, node_pa, nt, b_offset, num_succs, dsi, probs.get());
    } else {
      a_probs_->RMProbs(st, node_pa, nt, a_offset, num_succs, dsi, probs.get());
    }
    int s;
    for (s = 0; s < num_succs - 1; ++s) {
      double prob = probs[s];
      cum += prob;
      if (r < cum) break;
    }
    if (s == nodes[actual_pa]->CallSuccIndex()) {
      unique_ptr<Node * []> succ_nodes(new Node *[num_players_]);
      string action;
      for (int p = 0; p < num_players_; ++p) {
	int csi = nodes[p]->CallSuccIndex();
	succ_nodes[p] = nodes[p]->IthSucc(csi);
	if (p == 0) action = nodes[p]->ActionName(csi);
      }
      contributions[actual_pa] = last_bet_to;
      string new_action_sequence = action_sequence + action;
      Play(succ_nodes.get(), b_pos, contributions, last_bet_to, folded, num_remaining, actual_pa,
	   new_action_sequence, target_action_sequence, st);
    } else if (s == nodes[actual_pa]->FoldSuccIndex()) {
      unique_ptr<Node * []> succ_nodes(new Node *[num_players_]);
      string action;
      for (int p = 0; p < num_players_; ++p) {
	int fsi = nodes[p]->FoldSuccIndex();
	succ_nodes[p] = nodes[p]->IthSucc(fsi);
	if (p == 0) action = nodes[p]->ActionName(fsi);
      }
      folded[actual_pa] = true;
      string new_action_sequence = action_sequence + action;
      Play(succ_nodes.get(), b_pos, contributions, last_bet_to, folded, num_remaining - 1,
	   actual_pa, new_action_sequence, target_action_sequence, st);
    } else {
      Node *my_succ = nodes[actual_pa]->IthSucc(s);
      int new_bet_to = my_succ->LastBetTo();
      unique_ptr<Node * []> succ_nodes(new Node *[num_players_]);
      string action;
      for (int p = 0; p < num_players_; ++p) {
	int ps;
	Node *p_node = nodes[p];
	int p_num_succs = p_node->NumSuccs();
	for (ps = 0; ps < p_num_succs; ++ps) {
	  if (ps == p_node->CallSuccIndex() || ps == p_node->FoldSuccIndex()) {
	    continue;
	  }
	  if (p_node->IthSucc(ps)->LastBetTo() == new_bet_to) break;
	}
	if (ps == p_num_succs) {
	  fprintf(stderr, "No matching succ\n");
	  exit(-1);
	}
	succ_nodes[p] = nodes[p]->IthSucc(ps);
	if (p == 0) action = nodes[p]->ActionName(ps);
      }
      contributions[actual_pa] = new_bet_to;
      string new_action_sequence = action_sequence + action;
      Play(succ_nodes.get(), b_pos, contributions, new_bet_to, folded, num_remaining, actual_pa,
	   new_action_sequence, target_action_sequence, st);
    }
  }
}

static int PrecedingPlayer(int p) {
  if (p == 0) return Game::NumPlayers() - 1;
  else        return p - 1;
}

// Play one hand of duplicate, which is a pair of regular hands.  Return
// outcome from A's perspective.
void Player::PlayDuplicateHand(unsigned long long int h, const Card *cards,
			       const string &target_action_sequence) {
  unique_ptr<int []> contributions(new int[num_players_]);
  unique_ptr<bool []> folded(new bool[num_players_]);
  // Assume the big blind is last to act preflop
  // Assume the small blind is prior to the big blind
  int big_blind_p = PrecedingPlayer(Game::FirstToAct(0));
  int small_blind_p = PrecedingPlayer(big_blind_p);
  for (int b_pos = 0; b_pos < num_players_; ++b_pos) {
    for (int p = 0; p < num_players_; ++p) {
      folded[p] = false;
      if (p == small_blind_p) {
	contributions[p] = Game::SmallBlind();
      } else if (p == big_blind_p) {
	contributions[p] = Game::BigBlind();
      } else {
	contributions[p] = 0;
      }
    }
    unique_ptr<Node * []> nodes(new Node *[num_players_]);
    for (int p = 0; p < num_players_; ++p) {
      if (p == b_pos) nodes[p] = b_betting_trees_[p]->Root();
      else            nodes[p] = a_betting_trees_[p]->Root();
    }
    Play(nodes.get(), b_pos, contributions.get(), Game::BigBlind(), folded.get(), num_players_,
	 1000, "", target_action_sequence, -1);
  }
}

void Player::DealNCards(Card *cards, int n) {
  int max_card = Game::MaxCard();
  for (int i = 0; i < n; ++i) {
    Card c;
    while (true) {
      // c = RandBetween(0, max_card);
      double r;
      drand48_r(&rand_buf_, &r);
      c = (max_card + 1) * r;
      int j;
      for (j = 0; j < i; ++j) {
	if (cards[j] == c) break;
      }
      if (j == i) break;
    }
    cards[i] = c;
  }
}

void Player::SetHCPsAndBoards(Card **raw_hole_cards, const Card *raw_board) {
  int max_street = Game::MaxStreet();
  for (int st = 0; st <= max_street; ++st) {
    if (st == 0) {
      for (int p = 0; p < num_players_; ++p) {
	raw_hcps_[p][0] = HCPIndex(st, raw_hole_cards[p]);
      }
    } else {
      // Store the hole cards *after* the board cards
      int num_hole_cards = Game::NumCardsForStreet(0);
      int num_board_cards = Game::NumBoardCards(st);
      for (int p = 0; p < num_players_; ++p) {
	Card canon_board[5];
	Card canon_hole_cards[2];
	CanonicalizeCards(raw_board, raw_hole_cards[p], st, canon_board, canon_hole_cards);
	// Don't need to do this repeatedly
	if (p == 0) {
	  boards_[st] = BoardTree::LookupBoard(canon_board, st);
	}
	Card canon_cards[7];
	for (int i = 0; i < num_board_cards; ++i) {
	  canon_cards[num_hole_cards + i] = canon_board[i];
	}
	for (int i = 0; i < num_hole_cards; ++i) {
	  canon_cards[i] = canon_hole_cards[i];
	}
	raw_hcps_[p][st] = HCPIndex(st, canon_cards);
      }
    }
  }
}

void Player::Go(long long int num_duplicate_hands, const string &target_action_sequence) {
  sum_target_p1_outcomes_ = 0;
  num_target_p1_outcomes_ = 0;
  int max_street = Game::MaxStreet();
  int num_board_cards = Game::NumBoardCards(max_street);
  Card cards[100], hand_cards[7];
  Card **hole_cards = new Card *[num_players_];
  for (int p = 0; p < num_players_; ++p) {
    hole_cards[p] = new Card[2];
  }
  for (long long int h = 0; h < num_duplicate_hands; ++h) {
    // Assume 2 hole cards
    DealNCards(cards, num_board_cards + 2 * num_players_);
    for (int p = 0; p < num_players_; ++p) {
      SortCards(cards + 2 * p, 2);
    }
    int num = 2 * num_players_;
    for (int st = 1; st <= max_street; ++st) {
      int num_street_cards = Game::NumCardsForStreet(st);
      SortCards(cards + num, num_street_cards);
      num += num_street_cards;
    }
    for (int i = 0; i < num_board_cards; ++i) {
      hand_cards[i+2] = cards[i + 2 * num_players_];
    }
    for (int p = 0; p < num_players_; ++p) {
      hand_cards[0] = cards[2 * p];
      hand_cards[1] = cards[2 * p + 1];
      hvs_[p] = HandValueTree::Val(hand_cards);
      hole_cards[p][0] = cards[2 * p];
      hole_cards[p][1] = cards[2 * p + 1];
    }
    
    SetHCPsAndBoards(hole_cards, cards + 2 * num_players_);

    // PlayDuplicateHand() returns the result of a duplicate hand (which is
    // N hands if N is the number of players)
    PlayDuplicateHand(h, cards, target_action_sequence);
  }
  for (int p = 0; p < num_players_; ++p) {
    delete [] hole_cards[p];
  }
  delete [] hole_cards;
  if (num_target_p1_outcomes_ > 0) {
    double avg = sum_target_p1_outcomes_ / (double)num_target_p1_outcomes_;
    printf("Avg P1 target outcome: %f (%lli)\n", avg, num_target_p1_outcomes_);
    printf("P1 target reach: %f (%lli/%lli)\n",
	   num_target_p1_outcomes_ / (double)(2.0 * num_duplicate_hands),
	   num_target_p1_outcomes_, num_duplicate_hands);
  }
}

Player::Player(const BettingAbstraction &a_ba, const BettingAbstraction &b_ba,
	       const CardAbstraction &a_ca, const CardAbstraction &b_ca, const CFRConfig &a_cc,
	       const CFRConfig &b_cc, int a_it, int b_it) {
  a_buckets_ = new Buckets(a_ca, false);
  if (strcmp(a_ca.CardAbstractionName().c_str(), b_ca.CardAbstractionName().c_str())) {
    b_buckets_ = new Buckets(b_ca, false);
  } else {
    b_buckets_ = a_buckets_;
  }
  num_players_ = Game::NumPlayers();
  hvs_.reset(new int[num_players_]);
  winners_.reset(new bool[num_players_]);
  BoardTree::Create();
  BoardTree::CreateLookup();

  a_asymmetric_ = a_ba.Asymmetric();
  b_asymmetric_ = b_ba.Asymmetric();
  a_betting_trees_ = new BettingTree *[num_players_];
  b_betting_trees_ = new BettingTree *[num_players_];
  if (a_asymmetric_) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
    for (int asym_p = 0; asym_p < num_players_; ++asym_p) {
      a_betting_trees_[asym_p] = new BettingTree(a_ba, asym_p);
    }
  } else {
    a_betting_trees_[0] = new BettingTree(a_ba);
    for (int asym_p = 1; asym_p < num_players_; ++asym_p) {
      a_betting_trees_[asym_p] = a_betting_trees_[0];
    }
  }
  if (b_asymmetric_) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
    for (int asym_p = 0; asym_p < num_players_; ++asym_p) {
      b_betting_trees_[asym_p] = new BettingTree(b_ba, asym_p);
    }
  } else {
    b_betting_trees_[0] = new BettingTree(b_ba);
    for (int asym_p = 1; asym_p < num_players_; ++asym_p) {
      b_betting_trees_[asym_p] = b_betting_trees_[0];
    }
  }

  // Note assumption that we can use the betting tree for position 0
  a_probs_.reset(new CFRValues(nullptr, nullptr, 0, 0, *a_buckets_, a_betting_trees_[0]));
  b_probs_.reset(new CFRValues(nullptr, nullptr, 0, 0, *b_buckets_, b_betting_trees_[0]));

  char dir[500];
  
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  a_ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  a_ba.BettingAbstractionName().c_str(),
	  a_cc.CFRConfigName().c_str());
  // Note assumption that we can use the betting tree for position 0
  a_probs_->Read(dir, a_it, a_betting_trees_[0], "x", -1, true, false);

  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumPlayers(), b_ca.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), b_ba.BettingAbstractionName().c_str(),
	  b_cc.CFRConfigName().c_str());
  // Note assumption that we can use the betting tree for position 0
  b_probs_->Read(dir, b_it, b_betting_trees_[0], "x", -1, true, false);

#if 0
  // If we want to go back to supporting asymmetric systems, may need to have a separate
  // CFRValues object for each position for each player.
  char buf[100];
  for (int p = 0; p < num_players_; ++p) {
    if (a_asymmetric_) {
      sprintf(buf, ".p%u", p);
      strcat(dir, buf);
    }
    if (b_asymmetric_) {
      sprintf(buf, ".p%u", p);
      strcat(dir, buf);
    }
  }
#endif

  int max_street = Game::MaxStreet();
  boards_ = new int[max_street + 1];
  boards_[0] = 0;
  raw_hcps_ = new int *[num_players_];
  for (int p = 0; p < num_players_; ++p) {
    raw_hcps_[p] = new int[max_street + 1];
  }

  if (a_buckets_->None(max_street) || b_buckets_->None(max_street)) {
    int num_hole_card_pairs = Game::NumHoleCardPairs(max_street);
    int num_boards = BoardTree::NumBoards(max_street);
    sorted_hcps_ = new unsigned short *[num_boards];
    Card cards[7];
    int num_hole_cards = Game::NumCardsForStreet(0);
    int num_board_cards = Game::NumBoardCards(max_street);
    for (int bd = 0; bd < num_boards; ++bd) {
      const Card *board = BoardTree::Board(max_street, bd);
      for (int i = 0; i < num_board_cards; ++i) {
	cards[i + num_hole_cards] = board[i];
      }
      int sg = BoardTree::SuitGroups(max_street, bd);
      CanonicalCards hands(2, board, num_board_cards, sg, false);
      hands.SortByHandStrength(board);
      sorted_hcps_[bd] = new unsigned short[num_hole_card_pairs];
      for (int shcp = 0; shcp < num_hole_card_pairs; ++shcp) {
	const Card *hole_cards = hands.Cards(shcp);
	for (int i = 0; i < num_hole_cards; ++i) {
	  cards[i] = hole_cards[i];
	}
	int rhcp = HCPIndex(max_street, cards);
	sorted_hcps_[bd][rhcp] = shcp;
      }
    }
    fprintf(stderr, "Created sorted_hcps_\n");
  } else {
    sorted_hcps_ = nullptr;
    fprintf(stderr, "Not creating sorted_hcps_\n");
  }

  srand48_r(time(0), &rand_buf_);
}

Player::~Player(void) {
  if (sorted_hcps_) {
    int max_street = Game::MaxStreet();
    int num_boards = BoardTree::NumBoards(max_street);
    for (int bd = 0; bd < num_boards; ++bd) {
      delete [] sorted_hcps_[bd];
    }
    delete [] sorted_hcps_;
  }
  delete [] boards_;
  for (int p = 0; p < num_players_; ++p) {
    delete [] raw_hcps_[p];
  }
  delete [] raw_hcps_;
  if (b_buckets_ != a_buckets_) delete b_buckets_;
  delete a_buckets_;
  if (a_asymmetric_) {
    for (int asym_p = 0; asym_p < num_players_; ++asym_p) {
      delete a_betting_trees_[asym_p];
    }
  } else {
    delete a_betting_trees_[0];
  }
  delete [] a_betting_trees_;
  if (b_asymmetric_) {
    for (int asym_p = 0; asym_p < num_players_; ++asym_p) {
      delete b_betting_trees_[asym_p];
    }
  } else {
    delete b_betting_trees_[0];
  }
  delete [] b_betting_trees_;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <A card params> <B card params> "
	  "<A betting abstraction params> <B betting abstraction params> <A CFR params> "
	  "<B CFR params> <A it> <B it> <num duplicate hands> <action sequence>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 12) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> a_card_params = CreateCardAbstractionParams();
  a_card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    a_card_abstraction(new CardAbstraction(*a_card_params));
  unique_ptr<Params> b_card_params = CreateCardAbstractionParams();
  b_card_params->ReadFromFile(argv[3]);
  unique_ptr<CardAbstraction>
    b_card_abstraction(new CardAbstraction(*b_card_params));
  unique_ptr<Params> a_betting_params = CreateBettingAbstractionParams();
  a_betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    a_betting_abstraction(new BettingAbstraction(*a_betting_params));
  unique_ptr<Params> b_betting_params = CreateBettingAbstractionParams();
  b_betting_params->ReadFromFile(argv[5]);
  unique_ptr<BettingAbstraction>
    b_betting_abstraction(new BettingAbstraction(*b_betting_params));
  unique_ptr<Params> a_cfr_params = CreateCFRParams();
  a_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig>
    a_cfr_config(new CFRConfig(*a_cfr_params));
  unique_ptr<Params> b_cfr_params = CreateCFRParams();
  b_cfr_params->ReadFromFile(argv[7]);
  unique_ptr<CFRConfig>
    b_cfr_config(new CFRConfig(*b_cfr_params));

  int a_it, b_it;
  if (sscanf(argv[8], "%i", &a_it) != 1) Usage(argv[0]);
  if (sscanf(argv[9], "%i", &b_it) != 1) Usage(argv[0]);
  long long int num_duplicate_hands;
  if (sscanf(argv[10], "%lli", &num_duplicate_hands) != 1) Usage(argv[0]);
  string action_sequence = argv[11];
  HandValueTree::Create();

  Player player(*a_betting_abstraction, *b_betting_abstraction, *a_card_abstraction,
		*b_card_abstraction, *a_cfr_config, *b_cfr_config, a_it, b_it);
  player.Go(num_duplicate_hands, action_sequence);
}
