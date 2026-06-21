-- 共通の仕様モデル型。状態機械・フラグメント・trace の各証明が共有する。
-- conn.c の構造(ws_frame_header / ws_conn_state / ws_event)に1対1対応させる。
namespace WsProof.Spec

/-- RFC6455 opcode。 -/
inductive Opcode where
  | continuation -- 0x0
  | text         -- 0x1
  | binary       -- 0x2
  | close        -- 0x8
  | ping         -- 0x9
  | pong         -- 0xA
  | reserved (v : Nat) -- それ以外(未知/予約)
  deriving DecidableEq, Repr

/-- 制御フレームか(opcode の最上位ビット = 0x8 以上)。 -/
def Opcode.isControl : Opcode → Bool
  | .close | .ping | .pong => true
  | _ => false

/-- データ系の妥当 opcode か。 -/
def Opcode.isData : Opcode → Bool
  | .continuation | .text | .binary => true
  | _ => false

/-- パース済みフレームヘッダ + payload(仕様モデル)。 -/
structure Frame where
  fin     : Bool
  rsv1    : Bool
  rsv2    : Bool
  rsv3    : Bool
  opcode  : Opcode
  masked  : Bool
  payload : List UInt8
  deriving Repr

/-- 役割。server は masked を受理、unmasked を拒否。 -/
inductive Role where
  | server
  | client
  deriving DecidableEq, Repr

/-- 接続状態。conn.c の ws_conn_state に対応。 -/
inductive State where
  | open
  | closing
  | closed
  deriving DecidableEq, Repr

/-- step が外部に通知するイベント。conn.c の ws_event_type に対応。 -/
inductive Event where
  | none
  | message (op : Opcode) (data : List UInt8)
  | ping (data : List UInt8)
  | pong (data : List UInt8)
  | close (code : UInt16)
  | error
  deriving Repr

end WsProof.Spec
